#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "console/console.hpp"
#include "scheduler.hpp"

#include "game/game.hpp"

#include <utils/flags.hpp>
#include <utils/hook.hpp>

namespace dedicated
{
	namespace
	{
		constexpr std::array<std::uint8_t, 6> gamestate_guard_original_bytes{
			0x0F, 0x84, 0x4B, 0x01, 0x00, 0x00
		};

		utils::hook::detour cl_check_for_resend_hook;
		std::array<std::uint8_t, gamestate_guard_original_bytes.size()> gamestate_guard_actual_bytes{};
		bool gamestate_guard_patch_applied{};
		game::dvar_t* sv_lanOnly;

		bool apply_gamestate_guard_patch()
		{
			auto* const guard = reinterpret_cast<std::uint8_t*>(0xF44F3_g);
			std::memcpy(
				gamestate_guard_actual_bytes.data(),
				guard,
				gamestate_guard_actual_bytes.size()
			);

			if (gamestate_guard_actual_bytes != gamestate_guard_original_bytes)
			{
				return false;
			}

			// Dedicated remote clients must pass the cached local-address guard and continue
			// through the engine's unchanged server-ID and reliable-sequence checks.
			utils::hook::nop(guard, gamestate_guard_original_bytes.size());
			return true;
		}

		void log_gamestate_guard_patch_status()
		{
			if (gamestate_guard_patch_applied)
			{
				console::info(
					"Dedicated gamestate guard patch applied: verified 0F 84 4B 01 00 00 at "
					"0xF44F3 and NOPed exactly six bytes.\n"
				);
				return;
			}

			console::error(
				"Dedicated gamestate guard patch not applied: expected 0F 84 4B 01 00 00 at "
				"0xF44F3, found %02X %02X %02X %02X %02X %02X. Startup will continue.\n",
				gamestate_guard_actual_bytes[0],
				gamestate_guard_actual_bytes[1],
				gamestate_guard_actual_bytes[2],
				gamestate_guard_actual_bytes[3],
				gamestate_guard_actual_bytes[4],
				gamestate_guard_actual_bytes[5]
			);
		}

		void cl_check_for_resend_stub(const unsigned int)
		{
			// A dedicated process has no local frontend client to reconnect to its server.
		}

		std::string build_startup_map_command()
		{
			const auto map = utils::flags::get_plus_value("map");
			if (!map.has_value() || map.value().empty())
			{
				return {};
			}

			std::string command = "map ";
			command.append(map.value());

			const auto gametype = utils::flags::get_set_value("g_gametype");
			if (gametype.has_value() && !gametype.value().empty())
			{
				command.push_back(' ');
				command.append(gametype.value());
			}

			command.push_back('\n');
			return command;
		}

		void perform_online_game_init()
		{
			constexpr auto local_client = 0;

			game::Cbuf_AddText(local_client, "resetSplitscreenSignIn\n");
			game::Cbuf_AddText(local_client, "forcenosplitscreencontrol main_XBOXLIVE_3\n");

			game::Cbuf_AddText(local_client, "onlinegame 1\n");
			game::Cbuf_AddText(local_client, "systemlink 0\n");
			game::Cbuf_AddText(local_client, "splitscreen 0\n");
			game::Cbuf_AddText(local_client, "setgameprivatematch 0\n");

			game::Cbuf_AddText(local_client, "exec default_xboxlive.cfg\n");
			game::Cbuf_AddText(local_client, "virtuallobby\n");
		}

		void run_startup()
		{
			perform_online_game_init();

			console::info("==================================\n");
			console::info("S2x Dedicated Server\n");
			console::info("==================================\n");
			log_gamestate_guard_patch_status();
			console::set_title("S2x Dedicated Server");

			const auto command = build_startup_map_command();
			if (command.empty())
			{
				console::info(
					"Dedicated mode is active. "
					"Use +map <mapname> [+set g_gametype <gametype>] to auto-start a map.\n"
				);
				return;
			}

			console::info("Waiting for the virtual lobby to initialize...\n");

			const auto start_time = std::chrono::steady_clock::now();

			scheduler::schedule([command, start_time]()
			{
				if (game::virtual_lobby_loaded())
				{
					console::info("Virtual lobby initialized.\n");
					console::info("Executing dedicated startup command: %s", command.data());

					game::Cbuf_AddText(0, command.data());
					return scheduler::cond_end;
				}

				if (std::chrono::steady_clock::now() - start_time >= 30s)
				{
					console::error(
						"Timed out waiting for the virtual lobby. "
						"The startup map command was not executed.\n"
					);

					return scheduler::cond_end;
				}

				return scheduler::cond_continue;
			}, scheduler::pipeline::main, 100ms);
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::is_dedi())
			{
				return;
			}

			game::Dvar_RegisterBool("dedicated", true, game::DVAR_FLAG_READ);
			sv_lanOnly = game::Dvar_RegisterBool("sv_lanOnly", false, game::DVAR_FLAG_NONE);
			game::Dvar_RegisterBool("dedicated_use_direct_map_start", true, game::DVAR_FLAG_NONE);

			gamestate_guard_patch_applied = apply_gamestate_guard_patch();
			cl_check_for_resend_hook.create(game::CL_CheckForResend, cl_check_for_resend_stub);

			// TODO: Add killserver only after a safe S2 shutdown wrapper or flow is confirmed.
			scheduler::once(run_startup, scheduler::pipeline::main, 1s);
		}
	};
}

REGISTER_COMPONENT(dedicated::component)
