#include <std_include.hpp>

#include "scene.hpp"
#include "mesh.hpp"
#include "game/game.hpp"

namespace material_picker::scene
{
	namespace
	{
		geometry::vector point(const auto& vertex)
		{
			return {vertex.xyz[0], vertex.xyz[1], vertex.xyz[2]};
		}

		class scene_query
		{
		public:
			explicit scene_query(const geometry::ray& value) : ray(value)
			{
			}

			std::optional<picked_material> pick(
				const game::GfxWorld& world, const dynamic_geometry::snapshot& dynamic)
			{
				trace_dynamic(dynamic);
				trace_world(world);
				trace_props(world);
				if (hits.empty())
				{
					return {};
				}

				std::sort(hits.begin(), hits.end(),
					[](const auto& a, const auto& b)
					{
						return a.distance < b.distance;
					});
				picked_material picked;
				picked.render_geometry = true;
				picked.name = hits.front().name;
				picked.distance = nearest;
				picked.matches = std::move(hits);

				return picked;
			}

		private:
			static constexpr float layer_tolerance = 0.5f;
			const geometry::ray& ray;
			std::vector<render_material> hits;
			float nearest{trace_distance};

			float limit() const
			{
				return std::min(trace_distance, nearest + layer_tolerance);
			}

			void add_match(render_material match)
			{
				if (match.distance > limit())
				{
					return;
				}

				nearest = std::min(nearest, match.distance);
				std::erase_if(hits,
					[&](const auto& h)
					{
						return h.distance > limit();
					});
				const auto existing = std::find_if(hits.begin(), hits.end(),
					[&](const auto& h)
					{
						return h.name == match.name;
					});
				if (existing == hits.end())
				{
					hits.push_back(std::move(match));
				}
				else if (match.distance < existing->distance)
				{
					*existing = std::move(match);
				}
			}

			void add_hit(const game::Material* material, const char* model, const float distance)
			{
				if (!material || !material->name || distance > limit())
				{
					return;
				}

				auto match = copy_material(*material);
				match.distance = distance;
				match.source = model ? "Prop: " + std::string(model) : "Map surface / decal";
				add_match(std::move(match));
			}

			void trace_dynamic(const dynamic_geometry::snapshot& dynamic)
			{
				for (const auto& mesh : dynamic.meshes)
				{
					std::optional<float> distance;
					for (const auto& triangle : mesh.triangles)
					{
						if (const auto d = geometry::intersect_triangle(
								ray, triangle.a, triangle.b, triangle.c, distance.value_or(limit())))
						{
							distance = d;
						}
					}

					if (distance)
					{
						add_match({mesh.name, mesh.techset, mesh.images, mesh.source, *distance});
					}
				}
			}

			void trace_world(const game::GfxWorld& world)
			{
				for (std::uint32_t i = 0; i < world.surface_count; ++i)
				{
					const auto& bounds = world.surface_bounds[i].bounds;
					if (!geometry::intersects_bounds(ray, bounds.midpoint, bounds.half_size, limit()))
					{
						continue;
					}

					const auto& surface = world.surfaces[i];
					if (!surface.material || surface.zone >= std::size(world.zones) || surface.zone >= world.zone_count)
					{
						continue;
					}

					const auto* zone = world.zones[surface.zone];
					if (!zone || !zone->vertices || surface.first_vertex > zone->vertex_count
						|| surface.vertex_count > zone->vertex_count - surface.first_vertex
						|| surface.first_index > world.index_count
						|| std::uint32_t(surface.triangle_count) * 3 > world.index_count - surface.first_index)
					{
						continue;
					}

					const auto* vertices = zone->vertices + surface.first_vertex;
					const auto* indices = world.indices + surface.first_index;
					for (std::uint32_t t = 0; t < std::uint32_t(surface.triangle_count) * 3; t += 3)
					{
						if (indices[t] >= surface.vertex_count || indices[t + 1] >= surface.vertex_count
							|| indices[t + 2] >= surface.vertex_count)
						{
							continue;
						}

						if (const auto distance = geometry::intersect_triangle(ray, point(vertices[indices[t]]),
								point(vertices[indices[t + 1]]), point(vertices[indices[t + 2]]), limit()))
						{
							add_hit(surface.material, nullptr, *distance);
						}
					}
				}
			}

			void trace_props(const game::GfxWorld& world)
			{
				if (!world.models || !world.model_bounds)
				{
					return;
				}

				for (std::uint32_t i = 0; i < world.model_count; ++i)
				{
					const auto& bounds = world.model_bounds[i].bounds;
					if (!geometry::intersects_bounds(ray, bounds.midpoint, bounds.half_size, limit()))
					{
						continue;
					}

					const auto& instance = world.models[i];
					const auto* base = instance.model;
					if (!base || !base->materials)
					{
						continue;
					}

					const auto local = geometry::to_model(ray, instance.origin, instance.axis, instance.scale);
					const auto* selected_lod = mesh_geometry::resident_lod(*base);
					if (!local || !selected_lod)
					{
						continue;
					}

					// Composite LODs are concatenated child LODs; the native traversal uses
					// the selected child's material start in the outer material table.
					const auto& lod = *selected_lod;
					for (unsigned int j = 0; j < lod.surface_count; ++j)
					{
						const auto& surface = lod.surfaces[j];
						const auto* material = base->materials[lod.first_surface + j];
						const auto* indices = surface.indices;
						for (std::uint32_t t = 0; t < std::uint32_t(surface.triangle_count) * 3; t += 3)
						{
							if (indices[t] >= surface.vertex_count || indices[t + 1] >= surface.vertex_count
								|| indices[t + 2] >= surface.vertex_count)
							{
								continue;
							}

							if (const auto distance = geometry::intersect_triangle(*local,
									point(surface.vertices[indices[t]]), point(surface.vertices[indices[t + 1]]),
									point(surface.vertices[indices[t + 2]]), limit()))
							{
								add_hit(material, base->name, *distance);
							}
						}
					}
				}
			}
		};
	}

	std::optional<picked_material> pick(const geometry::ray& ray, const dynamic_geometry::snapshot& dynamic)
	{
		// The active GfxWorld is stored inline here, not as a pointer.
		const auto& world = *reinterpret_cast<const game::GfxWorld*>(game::select(0xE311B90, 0xBCD6010));
		if (!world.name || !world.surfaces || !world.surface_bounds || !world.indices)
		{
			return {};
		}

		return scene_query(ray).pick(world, dynamic);
	}
}
