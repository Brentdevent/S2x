#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "fps.hpp"
#include "scheduler.hpp"
#include "game/game.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

#include <array>
#include <atomic>
#include <chrono>

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

		void update_fps()
		{
			using clock = std::chrono::steady_clock;
			static clock::time_point previous{};
			static std::array<double, 32> frame_times{};
			static size_t index{};
			static size_t count{};
			static double total_ms{};

			const auto now = clock::now();
			if (previous == clock::time_point{})
			{
				previous = now;
				return;
			}

			const auto frame_ms = std::chrono::duration<double, std::milli>(now - previous).count();
			previous = now;
			if (frame_ms <= 0.0)
			{
				return;
			}

			total_ms -= frame_times[index];
			frame_times[index] = frame_ms;
			total_ms += frame_ms;
			index = (index + 1) % frame_times.size();
			count = std::min(count + 1, frame_times.size());

			// Average frame times, counting only real samples during startup.
			const auto value = 1000.0 * static_cast<double>(count) / total_ms;
			current_fps.store(static_cast<int>(std::min(value,
				static_cast<double>(std::numeric_limits<int>::max()))), std::memory_order_relaxed);
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

			const auto draw_fps = cg_draw_fps && cg_draw_fps->current.integer > 0;
			const auto ping = get_ping();
			if (!draw_fps && !ping)
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

			if (draw_fps)
			{
				const auto value = get_fps();
				const auto* color = value >= 60 ? fps_color_good : (value >= 30 ? fps_color_ok : fps_color_bad);
				draw(utils::string::va("%i FPS", value), color);
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

	int get_fps()
	{
		return current_fps.load(std::memory_order_relaxed);
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
