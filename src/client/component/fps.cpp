#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "scheduler.hpp"
#include "game/game.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>

namespace fps
{
	namespace
	{
		const game::dvar_t* cg_draw_fps{};
		const game::dvar_t* cg_draw_ping{};
		
		std::atomic<int> current_fps{};

		constexpr float fps_color_good[4] = {0.6f, 1.0f, 0.0f, 1.0f};
		constexpr float fps_color_ok[4] = {1.0f, 0.7f, 0.3f, 1.0f};
		constexpr float fps_color_bad[4] = {1.0f, 0.3f, 0.3f, 1.0f};
		
		constexpr float ping_color[4] = {1.0f, 1.0f, 1.0f, 0.65f};
		constexpr float diagnostic_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};

		struct frame_statistics
		{
			double mean_ms{};
			double min_ms{};
			double max_ms{};
			double deviation_ms{};
		};

		struct frame_history
		{
			std::array<double, 32> times{};
			size_t index{};
			size_t count{};

			void add(const double milliseconds)
			{
				if (!std::isfinite(milliseconds) || milliseconds <= 0.0)
				{
					return;
				}

				times[index] = milliseconds;
				index = (index + 1) % times.size();
				count = std::min(count + 1, times.size());
			}

			frame_statistics statistics() const
			{
				if (!count)
				{
					return {};
				}

				frame_statistics result{0.0, times[0], times[0], 0.0};
				for (size_t i = 0; i < count; ++i)
				{
					result.mean_ms += times[i] / static_cast<double>(count);
					result.min_ms = std::min(result.min_ms, times[i]);
					result.max_ms = std::max(result.max_ms, times[i]);
				}
				for (size_t i = 0; i < count; ++i)
				{
					// CoD4 CG_DrawFPS uses mean absolute deviation, not standard deviation.
					result.deviation_ms += std::abs(times[i] - result.mean_ms) / static_cast<double>(count);
				}
				return result;
			}
		};

		frame_statistics frame_stats{};

		int fps_for_frame_time(const double milliseconds)
		{
			if (!std::isfinite(milliseconds) || milliseconds <= 0.0)
			{
				return 0;
			}
			return static_cast<int>(std::min(1000.0 / milliseconds,
				static_cast<double>(std::numeric_limits<int>::max())));
		}

		void update_fps()
		{
			using clock = std::chrono::steady_clock;
			static clock::time_point previous{};
			static frame_history history{};

			const auto now = clock::now();
			if (previous == clock::time_point{})
			{
				previous = now;
				return;
			}

			const auto frame_ms = std::chrono::duration<double, std::milli>(now - previous).count();
			previous = now;
			history.add(frame_ms);
			frame_stats = history.statistics();

			// Average frame times, counting only real samples during startup.
			current_fps.store(fps_for_frame_time(frame_stats.mean_ms), std::memory_order_relaxed);
		}

		int get_fps()
		{
			return current_fps.load(std::memory_order_relaxed);
		}

		struct view_position
		{
			std::array<float, 3> origin{};
			float pitch{};
			float yaw{};
		};

		std::optional<view_position> get_view_position()
		{
			if (game::virtual_lobby_loaded())
			{
				return std::nullopt;
			}

			view_position view{};
			std::array<float, 3> forward{};
			if (game::environment::uses_multiplayer_binary())
			{
				if (!game::CL_IsLocalClientInGame(game::LOCAL_CLIENT_0))
				{
					return std::nullopt;
				}
				// These native wrappers call the protected CG_GetLocalClient from inside S2.
				game::CL_GetViewPos(game::LOCAL_CLIENT_0, view.origin.data());
				game::CL_GetViewForward(game::LOCAL_CLIENT_0, forward.data());
			}
			else
			{
				if (!game::SV_Loaded())
				{
					return std::nullopt;
				}
				std::copy_n(game::sp::refdef_view_origin.get(), view.origin.size(), view.origin.begin());
				std::copy_n(game::sp::refdef_view_forward.get(), forward.size(), forward.begin());
			}

			for (size_t i = 0; i < forward.size(); ++i)
			{
				if (!std::isfinite(view.origin[i]) || !std::isfinite(forward[i]))
				{
					return std::nullopt;
				}
			}
			const auto horizontal = std::hypot(forward[0], forward[1]);
			if (horizontal == 0.0f && forward[2] == 0.0f)
			{
				return std::nullopt;
			}
			constexpr float radians_to_degrees = 57.29577951308232f;
			view.pitch = std::atan2(-forward[2], horizontal) * radians_to_degrees;
			view.yaw = std::atan2(forward[1], forward[0]) * radians_to_degrees;
			if (view.yaw < 0.0f)
			{
				view.yaw += 360.0f;
			}
			return view;
		}

		std::optional<int> get_ping()
		{
			if (!cg_draw_ping || !cg_draw_ping->current.enabled || game::virtual_lobby_loaded() ||
				!game::CL_IsLocalClientInGame(game::LOCAL_CLIENT_0))
			{
				return std::nullopt;
			}

			const auto* client = game::CL_GetLocalClientActive.call_safe(game::LOCAL_CLIENT_0);
			if (!client)
			{
				return std::nullopt;
			}

			// S2 MP CL_ParseSnapshot (0x464C00): 0x465502 stores realtime minus
			// the acknowledged packet's send time in cl->snap.ping (+0x6330).
			// The calculation is reachable in stock S2; no S1 ping patch is needed.
			return std::max(0, *reinterpret_cast<const int*>(client + 0x6330));
		}

		void draw_counters()
		{
			update_fps();

			const auto mode = cg_draw_fps ? cg_draw_fps->current.integer : 0;
			const auto ping = get_ping();
			if (mode <= 0 && !ping)
			{
				return;
			}

			auto* font = game::R_RegisterFont("fonts/fira_mono_regular.ttf", 16);
			const auto* placement = game::ScrPlace_GetViewPlacement(game::LOCAL_CLIENT_0);
			if (!font || !placement)
			{
				return;
			}

			const auto* style = game::R_Font_GetLegacyFontStyle(6);
			auto y = font->pixelHeight * 1.2f;
			const auto draw = [&](const char* text, const float* color)
			{
				const auto x = placement->realViewportSize[0] - 10.0f - game::R_TextWidth(text, 0, font, 0, 0);
				game::R_AddCmdDrawText(text, std::numeric_limits<int>::max(), font, 0, 0, font->pixelHeight,
					x, y, 1.0f, 1.0f, 0.0f, color, style);
				y += font->pixelHeight * 1.2f;
			};

			if (mode > 0)
			{
				const auto value = get_fps();
				const auto* color = value >= 60 ? fps_color_good : (value >= 30 ? fps_color_ok : fps_color_bad);
				if (mode == 1)
				{
					draw(utils::string::va("%i FPS", value), color);
				}
				else
				{
					draw(utils::string::va("%i FPS (%i-%i)", value,
						fps_for_frame_time(frame_stats.max_ms), fps_for_frame_time(frame_stats.min_ms)), color);
					draw(utils::string::va("%.2f ms/frame (%.2f-%.2f)", frame_stats.mean_ms,
						frame_stats.min_ms, frame_stats.max_ms), diagnostic_color);
				}
			}
			if (mode >= 3)
			{
				draw(utils::string::va("Mean abs. deviation: %.2f ms", frame_stats.deviation_ms), diagnostic_color);
				draw(utils::string::va("Viewport: %.0f x %.0f", placement->realViewportSize[0],
					placement->realViewportSize[1]), diagnostic_color);
			}
			if (mode >= 4)
			{
				if (const auto view = get_view_position())
				{
					draw(utils::string::va("View: %.0f %.0f %.0f", view->origin[0], view->origin[1],
						view->origin[2]), diagnostic_color);
					draw(utils::string::va("Pitch: %.1f  Yaw: %.1f", view->pitch, view->yaw), diagnostic_color);
				}
			}
			if (ping)
			{
				draw(utils::string::va("Ping: %i", *ping), ping_color);
			}
		}

		game::dvar_t* register_draw_fps(const char* name, const char* const* values, const int default_index,
			const game::DvarFlags flags)
		{
			// Keep the stock enum, default and engine name (2377), including config values.
			auto* dvar = game::Dvar_RegisterEnum(name, values, default_index,
				static_cast<game::DvarFlags>(flags | game::DVAR_FLAG_SAVED));
			cg_draw_fps = dvar;
			return dvar;
		}

		int playlist_dvar_command()
		{
			// Playlist rules include "2377 0" when entering or returning to the lobby.
			// Consume only that dvar here; console and config commands still use the engine.
			if (cg_draw_fps && game::Dvar_FindMalleableVar(game::Cmd_Argv(0)) == cg_draw_fps)
			{
				return 1;
			}

			return game::Dvar_Command();
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated())
			{
				return;
			}

			utils::hook::call(game::select(0x41D874, 0x212827), register_draw_fps);
			
			if (game::environment::uses_multiplayer_binary())
			{
				cg_draw_ping = game::Dvar_RegisterBool("cg_drawPing", false, game::DVAR_FLAG_SAVED);

				// Dvar_Command calls in MP Playlist_RunRules (0x6563D0).
				for (const auto address : {0x65665E_g, 0x656922_g, 0x656BB2_g, 0x656E52_g})
				{
					utils::hook::call(address, playlist_dvar_command);
				}
			}

			scheduler::loop(draw_counters, scheduler::renderer);
		}
	};
}

REGISTER_COMPONENT(fps::component)
