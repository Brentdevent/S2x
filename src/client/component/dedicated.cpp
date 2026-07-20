#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "console/console.hpp"
#include "dedicated_party.hpp"
#include "scheduler.hpp"

#include "game/game.hpp"

#include "component/gsc/script_extension.hpp"

#include <utils/flags.hpp>
#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace dedicated
{
	namespace
	{
		utils::hook::detour cl_check_for_resend_hook;

		void disable_p2p_auth_ticket_validation()
		{
			constexpr std::array<std::uint8_t, 5> mark_authenticated{
				0xC6, 0x44, 0x24, 0x70, 0x01
			};

			utils::hook::copy(
				0x486E87_g,
				mark_authenticated.data(),
				mark_authenticated.size()
			);

			utils::hook::jump(0x486E8C_g, 0x486FA8_g);
			utils::hook::nop(0x486E91_g, 1);
		}

		void cl_check_for_resend_stub(const unsigned int local_client_num)
		{
			if (game::virtual_lobby_loaded())
			{
				// PartyHost_Frame requires the frontend owner to finish its virtual-lobby
				// connection before the native prematch state machine can start a match.
				cl_check_for_resend_hook.invoke<void>(local_client_num);
			}

			// Virtual-lobby shutdown clears the loaded flag before gameplay reconnects,
			// keeping the dedicated process out of gameplay server-client slots.
		}

		void gscr_is_using_match_rules_data_stub()
		{
			game::Scr_AddInt(0);
		}

		void queue_startup_config(const int local_client)
		{
			const auto config = utils::flags::get_plus_value("exec");
			if (config)
			{
				console::info("Queueing dedicated startup config '%s'.\n", config->data());
				game::Cbuf_AddText(local_client, utils::string::va("exec %s\n", config->data()));
			}
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
			queue_startup_config(local_client);
			game::Cbuf_AddText(local_client, "virtuallobby\n");
		}

		void run_startup()
		{
			perform_online_game_init();

			console::info("==================================\n");
			console::info("S2x Dedicated Server\n");
			console::info("==================================\n");
			console::set_title("S2x Dedicated Server");

			console::info("Waiting for the virtual lobby to initialize...\n");
			scheduler::schedule([]
			{
				if (!game::virtual_lobby_loaded())
				{
					return scheduler::cond_continue;
				}

				console::info("Virtual lobby initialized.\n");
				dedicated_party::start();
				return scheduler::cond_end;
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
			game::Dvar_RegisterBool("sv_lanOnly", false, game::DVAR_FLAG_NONE);

			cl_check_for_resend_hook.create(game::CL_CheckForResend, cl_check_for_resend_stub);
			disable_p2p_auth_ticket_validation();
			gsc::override_function("isusingmatchrulesdata", gscr_is_using_match_rules_data_stub);

			// Bypass the gamestate guard
			utils::hook::nop(0xF44F3_g, 6);

			// TODO: Add killserver only after a safe S2 shutdown wrapper or flow is confirmed.
			scheduler::once(run_startup, scheduler::pipeline::main, 1s);
		}
	};
}

REGISTER_COMPONENT(dedicated::component)
