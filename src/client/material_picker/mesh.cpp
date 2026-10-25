#include <std_include.hpp>

#include "mesh.hpp"
#include <cmath>

namespace material_picker::mesh_geometry
{
	namespace
	{
		geometry::vector position(const auto& vertex)
		{
			return {vertex.xyz[0], vertex.xyz[1], vertex.xyz[2]};
		}
	}

	const game::XModelLodInfo* resident_lod(const game::XModelBase& base)
	{
		if (!base.model)
		{
			return nullptr;
		}

		const unsigned int count = (base.flags & 0x200) ? base.part_count : 1;
		for (unsigned int part = 0; part < count; ++part)
		{
			const auto* model = base.model;
			if (base.flags & 0x200)
			{
				const auto* child = reinterpret_cast<const game::XModelCompositePart*>(base.model)[part].base;
				if (!child || (child->flags & 0x200))
				{
					continue;
				}

				model = child->model;
			}

			if (!model || model->lod_count > std::size(model->lods))
			{
				continue;
			}

			for (unsigned int l = 0; l < model->lod_count; ++l)
			{
				const auto& lod = model->lods[l];
				if (!lod.surfaces || !lod.surface_count)
				{
					continue;
				}

				bool complete = true;
				for (unsigned int j = 0; j < lod.surface_count; ++j)
				{
					complete &= lod.surfaces[j].indices && lod.surfaces[j].vertices;
				}

				if (complete)
				{
					return &lod;
				}
			}
		}

		return nullptr;
	}

	std::vector<geometry::triangle> pose_surface(const game::DObj& object, const game::XSurface& surface,
		const game::SkinSurfaceContext& context, const geometry::vector& view_offset)
	{
		std::vector<geometry::triangle> result;
		if (!object.bones || (surface.flags & 4) || !surface.indices || !surface.vertices || !surface.rigid_lists
			|| !context.base_pose)
		{
			return result;
		}

		for (unsigned int r = 0; r < surface.rigid_count; ++r)
		{
			const auto& list = surface.rigid_lists[r];
			const auto local_bone = list.bone_offset >> 6;
			const auto bone = context.bone_offset + local_bone;
			if (bone >= object.bone_count || bone >= std::size(object.hidden) * 32
				|| (object.hidden[bone >> 5] & (0x80000000u >> (bone & 31)))
				|| list.first_triangle > surface.triangle_count
				|| list.triangle_count > surface.triangle_count - list.first_triangle)
			{
				continue;
			}

			const auto bind = geometry::bone_matrix(context.base_pose[local_bone]);
			const auto pose = geometry::bone_matrix(object.bones[bone]);
			const auto posed = [&](const unsigned int index)
			{
				return geometry::pose_vertex(position(surface.vertices[index]), bind, pose, view_offset);
			};

			for (unsigned int t = list.first_triangle; t < unsigned(list.first_triangle) + list.triangle_count; ++t)
			{
				const auto* indices = surface.indices + 3 * t;
				if (indices[0] >= surface.vertex_count || indices[1] >= surface.vertex_count
					|| indices[2] >= surface.vertex_count)
				{
					continue;
				}

				result.push_back({posed(indices[0]), posed(indices[1]), posed(indices[2])});
			}
		}

		return result;
	}

	// Glass's final vertex writer has no draw descriptor argument. Match the
	// alpha conversion/fade in 0x4FC4E0 / 0x504350 before accepting that mark.
	bool mark_visible(const game::FxMark& mark, const std::int32_t time)
	{
		if (!std::isfinite(mark.alpha))
		{
			return false;
		}

		const auto alpha = std::clamp(static_cast<int>(std::clamp(mark.alpha, 0.0f, 1.0f) * 255.0f + 0.5f), 0, 255);
		const auto elapsed = std::int64_t(time) - mark.created_time;
		const auto fade_in = 125 * mark.fade_in, fade_out = 125 * mark.fade_out;
		if (elapsed < 0)
		{
			return false;
		}

		if (elapsed < fade_in)
		{
			return elapsed * alpha / fade_in > 0;
		}

		if (!fade_out)
		{
			return alpha > 0;
		}

		if (elapsed - fade_in >= fade_out)
		{
			return false;
		}

		auto factor = 1.0f - float(elapsed - fade_in) / float(fade_out);
		if (mark.flags & 1)
		{
			factor *= factor;
		}

		return static_cast<int>(alpha * factor) > 0;
	}

	std::vector<geometry::triangle> mark_triangles(const game::FxMark& mark, const game::FxMarkVertex* vertices)
	{
		std::vector<geometry::triangle> result;
		if (!vertices)
		{
			return result;
		}

		auto remaining = unsigned(mark.triangle_count);
		for (auto* block = mark.triangles; block && remaining; block = block->next)
		{
			if (!block->count || block->count > remaining)
			{
				break;
			}

			const auto* indices =
				reinterpret_cast<const std::uint16_t*>(reinterpret_cast<const std::uint8_t*>(block) + 18);
			for (unsigned int t = 0; t < unsigned(block->count) * 3; t += 3)
			{
				if (indices[t] >= mark.vertex_count || indices[t + 1] >= mark.vertex_count
					|| indices[t + 2] >= mark.vertex_count)
				{
					continue;
				}

				result.push_back({position(vertices[indices[t]]), position(vertices[indices[t + 1]]),
					position(vertices[indices[t + 2]])});
			}

			remaining -= block->count;
		}

		return result;
	}
}
