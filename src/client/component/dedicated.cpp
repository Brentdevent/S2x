#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "console/console.hpp"
#include "scheduler.hpp"

#include "game/game.hpp"

#include <utils/flags.hpp>

namespace dedicated
{
	namespace
	{
		game::dvar_t* sv_lanOnly;

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

			// TODO: Confirm S2-specific dedicated-safe hooks before disabling renderer, UI, LUI, sound, config,
			// sys_error, host migration, or local-client reconnect paths.
			// TODO: Add killserver only after a safe S2 shutdown wrapper or flow is confirmed.

			scheduler::once(run_startup, scheduler::pipeline::main, 1s);
		}
	};
}

REGISTER_COMPONENT(dedicated::component)
