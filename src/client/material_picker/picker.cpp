#include <std_include.hpp>

#include "picker.hpp"
#include "collision.hpp"
#include "scene.hpp"
#include "game/game.hpp"
#include "game/lookup/dvars.hpp"
#include "game/types/rendering.hpp"
#include "component/command.hpp"
#include "component/console/console.hpp"
#include "component/scheduler.hpp"
#include <utils/string.hpp>
#include <cmath>

namespace material_picker
{
	namespace
	{
		game::dvar_t* cg_draw_material{};
		std::atomic_bool print_requested{};

		bool in_game()
		{
			if (game::virtual_lobby_loaded())
			{
				return false;
			}

			return game::environment::uses_multiplayer_binary()
				? game::CL_IsLocalClientInGame(game::LOCAL_CLIENT_0)
				: game::SV_Loaded();
		}

		std::optional<geometry::ray> view_ray()
		{
			geometry::ray ray;
			if (game::environment::uses_multiplayer_binary())
			{
				game::CL_GetViewPos(game::LOCAL_CLIENT_0, ray.origin.data());
				game::CL_GetViewForward(game::LOCAL_CLIENT_0, ray.direction.data());
			}
			else
			{
				std::copy_n(game::sp::refdef_view_origin.get(), ray.origin.size(), ray.origin.begin());
				std::copy_n(game::sp::refdef_view_forward.get(), ray.direction.size(), ray.direction.begin());
			}

			const auto length_squared = geometry::dot(ray.direction, ray.direction);
			if (!std::isfinite(length_squared) || length_squared < 0.0001f)
			{
				return {};
			}

			for (unsigned int i = 0; i < 3; ++i)
			{
				if (!std::isfinite(ray.origin[i]))
				{
					return {};
				}

				ray.direction[i] /= static_cast<float>(std::sqrt(length_squared));
			}

			return ray;
		}

		std::optional<picked_material> pick(const geometry::ray& ray, const dynamic_geometry::snapshot& dynamic)
		{
			if (auto rendered = scene::pick(ray, dynamic))
			{
				return rendered;
			}

			return collision::pick(ray);
		}

		void draw(const picked_material* picked, const int mode)
		{
			auto* font = game::R_RegisterFont("fonts/fira_mono_regular.ttf", 16);
			const auto* placement = game::ScrPlace_GetViewPlacement(game::LOCAL_CLIENT_0);
			if (!font || !placement)
			{
				return;
			}

			const auto* style = game::R_Font_GetLegacyFontStyle(6);
			constexpr float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
			auto y = placement->realViewportSize[1] * 0.55f;
			const auto line = [&](const std::string& text)
			{
				if (y + font->pixelHeight >= placement->realViewportSize[1])
				{
					return;
				}

				game::R_TextWidthWithFlags(text.c_str(), 0, font, 0, 0, font->pixelHeight, 0.0f, 1);
				game::R_AddCmdDrawText(
					text.c_str(), 0x7FFFFFFF, font, 0, 0, font->pixelHeight, 12.0f, y, 1.0f, 1.0f, 0.0f, color, style);
				y += font->pixelHeight * 1.2f;
			};

			if (!picked)
			{
				line("Material: no supported surface under crosshair");

				return;
			}

			line("Material: " + picked->name);
			if (!picked->render_geometry)
			{
				line(utils::string::va("Collision fallback: %.0f units | surface: %08X | contents: %016llX",
					picked->distance, picked->surface_flags, picked->contents));
			}

			if (picked->matches.empty())
			{
				line("No matching loaded render material (collision-only surface or different asset name)");

				return;
			}

			if (picked->matches.size() > 1)
			{
				line(picked->render_geometry ? "Overlapping surfaces" : "Several render variants match");
			}

			for (const auto& match : picked->matches)
			{
				if (match.name != picked->name)
				{
					line("Material: " + match.name);
				}

				if (picked->render_geometry)
				{
					line(match.source + utils::string::va(" | %.1f units", match.distance));
				}

				line(utils::string::va("Techset: %s", match.techset.empty() ? "<none>" : match.techset.c_str()));
				if (mode >= 2)
				{
					line(utils::string::va("%zu image bindings", match.images.size()));
					for (const auto& image : match.images)
					{
						line(image);
					}
				}
			}
		}

		void print_selection(const picked_material* picked, const bool truncated)
		{
			if (truncated)
			{
				console::info("[material] Geometry capture limit reached; some dynamic surfaces were skipped.\n");
			}

			if (!picked)
			{
				console::info("[material] No supported surface under crosshair. Load a level first.\n");

				return;
			}

			if (!picked->render_geometry)
			{
				console::info("[material] Collision fallback: %s | %.1f units | surface %08X | contents %016llX\n",
					picked->name.c_str(), picked->distance, picked->surface_flags, picked->contents);
			}

			if (picked->matches.empty())
			{
				console::info("[material] No matching loaded render material.\n");

				return;
			}

			if (!picked->render_geometry && picked->matches.size() > 1)
			{
				console::info("[material] %zu render variants match; these are candidates.\n", picked->matches.size());
			}

			for (const auto& match : picked->matches)
			{
				if (picked->render_geometry)
				{
					console::info("[material] %s | %.1f units\n", match.source.c_str(), match.distance);
				}

				console::info("materialDump \"%s\"\n", match.name.c_str());
				console::info("  Techset: %s\n", match.techset.empty() ? "<none>" : match.techset.c_str());
				for (const auto& image : match.images)
				{
					console::info("  %s\n", image.c_str());
				}
			}
		}

		void update()
		{
			// Capture one frame per sample, then disable worker capture until the next
			// 10 Hz query. The HUD redraws the cached selection every frame.
			static std::optional<picked_material> picked;
			static std::optional<geometry::ray> pending_ray;
			static std::string map_name;
			static std::chrono::steady_clock::time_point next_sample{};
			static bool print_pending{};
			static bool active{};

			const auto requested = print_requested.exchange(false);
			const auto mode = cg_draw_material ? cg_draw_material->current.integer : 0;
			if (!mode && !requested && !print_pending && !active)
			{
				return;
			}

			if (!in_game() || (!mode && !requested && !print_pending))
			{
				picked.reset();
				dynamic_geometry::clear();
				pending_ray.reset();
				map_name.clear();
				next_sample = {};
				active = false;
				if (print_pending || requested)
				{
					console::info("[material] No supported surface under crosshair. Load a level first.\n");
				}

				print_pending = false;

				return;
			}

			active = true;
			try
			{
				const auto* world = reinterpret_cast<const game::GfxWorld*>(game::select(0xE311B90, 0xBCD6010));
				const std::string current_map = world->name ? world->name : "";
				if (map_name != current_map)
				{
					dynamic_geometry::clear();
					pending_ray.reset();
					picked.reset();
					map_name = current_map;
					next_sample = {};
				}

				if (pending_ray)
				{
					const auto dynamic = dynamic_geometry::advance({});
					picked = pick(*pending_ray, dynamic);
					pending_ray.reset();
					if (picked && !picked->render_geometry)
					{
						resolve_material(*picked);
					}

					if (print_pending)
					{
						print_selection(picked ? &*picked : nullptr, dynamic.truncated);
						print_pending = false;
					}
				}

				print_pending |= requested;
				const auto now = std::chrono::steady_clock::now();
				if (print_pending || (mode && now >= next_sample))
				{
					pending_ray = view_ray();
					dynamic_geometry::advance(pending_ray);
					next_sample = now + 100ms;
					if (!pending_ray)
					{
						picked.reset();
						if (print_pending)
						{
							print_selection(nullptr, false);
							print_pending = false;
						}
					}
				}

				if (mode && in_game())
				{
					draw(picked ? &*picked : nullptr, mode);
				}
			}
			catch (const std::exception& error)
			{
				// Avoid repeating an error every frame.
				picked.reset();
				pending_ray.reset();
				dynamic_geometry::clear();
				print_pending = false;
				map_name.clear();
				game::Dvar_SetInt(cg_draw_material, 0);
				active = false;
				console::error("[material] Picker disabled: %s\n", error.what());
			}
		}
	}

	void initialize()
	{
		// Registration and console lookup must use the same mapped engine name.
		const auto name = game::lookup::dvars::resolve_engine_name("cg_drawMaterial");
		cg_draw_material = game::Dvar_RegisterInt(name.data(), 0, 0, 2, game::DVAR_FLAG_NONE);

		collision::initialize();
		dynamic_geometry::install();

		command::add("materialInfo",
			[]
			{
				print_requested = true;
			});

		scheduler::loop(update, scheduler::renderer);
	}
}
