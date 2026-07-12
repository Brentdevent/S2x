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
		utils::hook::detour cl_check_for_resend_hook;

		game::dvar_t* sv_lanOnly;

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
			scheduler::schedule([command]()
			{
				if (game::virtual_lobby_loaded())
				{
					console::info("Virtual lobby initialized.\n");
					console::info("Executing dedicated startup command: %s", command.data());

					game::Cbuf_AddText(0, command.data());
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

			cl_check_for_resend_hook.create(game::CL_CheckForResend, cl_check_for_resend_stub);

			// Bypass the gamestate guard
			utils::hook::nop(0xF44F3_g, 6);

			// TODO: Add killserver only after a safe S2 shutdown wrapper or flow is confirmed.
			scheduler::once(run_startup, scheduler::pipeline::main, 1s);
		}
	};
}

REGISTER_COMPONENT(dedicated::component)
