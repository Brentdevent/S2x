#include <std_include.hpp>

#include "dynamic.hpp"
#include "selection.hpp"
#include "mesh.hpp"
#include "skinning.hpp"
#include "game/game.hpp"
#include "game/types/images.hpp"
#include "game/types/rendering.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace material_picker::dynamic_geometry
{
	namespace
	{
		frame_capture<mesh> captured;
		utils::hook::detour skin_object_hook;
		utils::hook::detour skin_surface_hook;
		utils::hook::detour mark_world_hook;
		utils::hook::detour mark_animated_hook;
		utils::hook::detour mark_glass_hook;

		struct object_capture
		{
			frame_capture<mesh>::ticket request;
			std::uint32_t frame;
			bool linear_skinning;
		};

		thread_local const object_capture* current_object{};

		std::uint32_t frame_number()
		{
			return *reinterpret_cast<const std::uint32_t*>(game::select(0xE34BA90, 0xBD0FF10));
		}

		void describe(mesh& result, const game::Material* material, const char* source)
		{
			auto description = copy_material(*material);
			result.name = std::move(description.name);
			result.techset = std::move(description.techset);
			result.images = std::move(description.images);
			result.source = source;
		}

		bool linear_blending(const game::DObj& object)
		{
			if (!object.models)
			{
				return false;
			}

			// DObj model entries are 16 bytes in MP and 40 bytes in SP.
			// Use the same accessor and blend flag as the native skinning path.
			for (unsigned int i = 0; i < object.model_count; ++i)
			{
				const auto* model = utils::hook::invoke<const game::XModelBase*>(
					game::select(0x7BEF60, 0x51B420), &object, i);
				if (model && (model->flags & 1))
				{
					return true;
				}
			}

			return false;
		}

		std::uint64_t skin_object_stub(void* scene, const game::DObj* object, const void* bones)
		{
			// The caller holds the DObj reference until this call returns.
			// The pose and selected LODs have already been computed.
			const auto request = captured.acquire();
			std::optional<object_capture> capture;
			if (request)
			{
				game::Bounds bounds{};
				utils::hook::invoke<void>(
					game::select(0xB8720, 0x643890), static_cast<const std::uint8_t*>(scene) + 72, &bounds);

				if (geometry::intersects_bounds(request->ray, bounds.midpoint, bounds.half_size, 8192.0f))
				{
					capture = object_capture{*request, frame_number(), object && linear_blending(*object)};
				}
			}

			struct scope
			{
				const object_capture* previous{current_object};
				~scope()
				{
					current_object = previous;
				}
			} restore;
			current_object = capture ? &*capture : nullptr;

			return skin_object_hook.invoke<std::uint64_t>(scene, object, bones);
		}

		void capture_surface(const game::DObj* object, const game::Material* material,
			const game::XSurface* surface, const game::SkinSurfaceContext* context)
		{
			if (!current_object || !object || !object->bones || !material || !material->name || !surface || !context
				|| !context->base_pose)
			{
				return;
			}

			mesh result;
			const auto* offset = reinterpret_cast<const float*>(game::select(0x1357DC0, 0xDC1BD40));
			const geometry::vector view_offset{offset[0], offset[1], offset[2]};
			const auto weighted = (surface->flags & 4) != 0;
			result.triangles = weighted
				? skinning::pose_surface(*object, *surface, *context, view_offset, current_object->linear_skinning)
				: mesh_geometry::pose_surface(*object, *surface, *context, view_offset);
			if (result.triangles.empty())
			{
				return;
			}

			describe(result, material, weighted ? "Weighted / skinned prop" : "Animated / composite prop");
			const auto count = result.triangles.size();
			captured.submit(current_object->request, current_object->frame, std::move(result), count);
		}

		std::uint64_t skin_surface_stub(const game::DObj* object, const game::Material* material,
			const game::XSurface* surface, const game::SkinSurfaceContext* context, void* counts, void* output,
			void* surfaces)
		{
			const auto result =
				skin_surface_hook.invoke<std::uint64_t>(object, material, surface, context, counts, output, surfaces);
			try
			{
				capture_surface(object, material, surface, context);
			}
			catch (...)
			{
				// Inspector failures must not unwind through an engine worker.
			}

			return result;
		}

		bool intersects_mark(const geometry::ray& ray, const game::FxMark& mark, const game::FxMarkVertex* vertices)
		{
			geometry::vector minimum{vertices[0].xyz[0], vertices[0].xyz[1], vertices[0].xyz[2]};
			auto maximum = minimum;
			for (unsigned int i = 1; i < mark.vertex_count; ++i)
			{
				for (unsigned int axis = 0; axis < 3; ++axis)
				{
					minimum[axis] = std::min(minimum[axis], vertices[i].xyz[axis]);
					maximum[axis] = std::max(maximum[axis], vertices[i].xyz[axis]);
				}
			}

			geometry::vector midpoint, half_size;
			for (unsigned int axis = 0; axis < 3; ++axis)
			{
				midpoint[axis] = (minimum[axis] + maximum[axis]) * 0.5f;
				half_size[axis] = std::max(midpoint[axis] - minimum[axis], maximum[axis] - midpoint[axis]);
			}

			return geometry::intersects_bounds(ray, midpoint.data(), half_size.data(), 8192.0f);
		}

		void capture_mark(const frame_capture<mesh>::ticket& request, const std::uint32_t frame,
			const game::FxMark* mark, const std::uint16_t first_vertex)
		{
			if (!mark || !mark->material || !mark->material->name || !mark->triangles || !mark->vertex_count)
			{
				return;
			}

			// Read the finished, world-space vertices after the original writer.
			// This includes animated attachments, placement, cached vertices and
			// glass offsets without recreating mark-system lifetime rules.
			const auto* vertices =
				utils::hook::invoke<const game::FxMarkVertex*>(game::select(0x8AB740, 0x62A1C0), first_vertex);
			if (!vertices || !intersects_mark(request.ray, *mark, vertices))
			{
				return;
			}

			mesh result;
			result.triangles = mesh_geometry::mark_triangles(*mark, vertices);
			if (result.triangles.empty())
			{
				return;
			}

			describe(result, mark->material, "Temporary decal / bullet impact");
			const auto count = result.triangles.size();
			captured.submit(request, frame, std::move(result), count);
		}

		bool visible(const void* draw)
		{
			// The native draw setup writes the final fade alpha at +19.
			return draw && static_cast<const std::uint8_t*>(draw)[19] != 0;
		}

		std::uint64_t mark_world_stub(
			void* system, const game::FxMark* mark, const std::uint16_t vertex, void* camera, void* draw)
		{
			const auto request = captured.acquire();
			const auto frame = request ? frame_number() : 0;
			const auto result = mark_world_hook.invoke<std::uint64_t>(system, mark, vertex, camera, draw);
			try
			{
				if (request && visible(draw))
				{
					capture_mark(*request, frame, mark, vertex);
				}
			}
			catch (...)
			{
			}

			return result;
		}

		std::uint64_t mark_animated_stub(void* system, const game::FxMark* mark, const std::uint16_t vertex,
			void* draw, void* camera, void* transform)
		{
			const auto request = captured.acquire();
			const auto frame = request ? frame_number() : 0;
			const auto result = mark_animated_hook.invoke<std::uint64_t>(system, mark, vertex, draw, camera, transform);
			try
			{
				if (request && visible(draw))
				{
					capture_mark(*request, frame, mark, vertex);
				}
			}
			catch (...)
			{
			}

			return result;
		}

		std::uint64_t mark_glass_stub(
			void* system, const game::FxMark* mark, const std::uint16_t vertex, const float normal_sign, void* offset)
		{
			const auto request = captured.acquire();
			const auto frame = request ? frame_number() : 0;
			const auto result = mark_glass_hook.invoke<std::uint64_t>(system, mark, vertex, normal_sign, offset);
			try
			{
				if (request && mark && mesh_geometry::mark_visible(*mark, *static_cast<const std::int32_t*>(system)))
				{
					capture_mark(*request, frame, mark, vertex);
				}
			}
			catch (...)
			{
			}

			return result;
		}
	}

	void install()
	{
		// Capture while the native caller owns the geometry.
		skin_object_hook.create(game::select(0x874060, 0x5F39F0), skin_object_stub);
		skin_surface_hook.create(game::select(0x873E60, 0x5F37F0), skin_surface_stub);
		mark_world_hook.create(game::select(0x507110, 0x2EBCB0), mark_world_stub);
		mark_animated_hook.create(game::select(0x505B50, 0x2EA6F0), mark_animated_stub);
		mark_glass_hook.create(game::select(0x500C50, 0x2E57F0), mark_glass_stub);
	}

	void clear()
	{
		captured.advance(0, {});
	}

	snapshot advance(const std::optional<geometry::ray>& next)
	{
		return captured.advance(frame_number(), next);
	}
}
