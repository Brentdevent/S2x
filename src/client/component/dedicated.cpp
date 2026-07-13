#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "console/console.hpp"
#include "party.hpp"
#include "scheduler.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>

namespace dedicated
{
	namespace
	{
		utils::hook::detour cl_check_for_resend_hook;

		game::dvar_t* sv_lanOnly;

		void disable_p2p_auth_ticket_validation()
		{
			constexpr std::array<std::uint8_t, 11> expected{
				0x44, 0x38, 0x7C, 0x24, 0x70,
				0x0F, 0x85, 0x16, 0x01, 0x00, 0x00
			};
			const auto address = reinterpret_cast<const std::uint8_t*>(0x486E87_g);
			if (!std::equal(expected.begin(), expected.end(), address))
			{
				console::error("Dedicated party: P2P-auth bypass was not applied: unexpected game bytes.\n");
				return;
			}

			// Preserve the stock host-identity setup used by pa_joined, then treat this
			// raw dedicated member as authenticated and continue with member validation.
			constexpr std::array<std::uint8_t, 5> mark_authenticated{
				0xC6, 0x44, 0x24, 0x70, 0x01
			};
			utils::hook::copy(0x486E87_g, mark_authenticated.data(), mark_authenticated.size());
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

			console::info("Waiting for the virtual lobby to initialize...\n");
			scheduler::schedule([private_party_requested = false,
				private_party_start_time = std::chrono::steady_clock::time_point{}]() mutable
			{
				if (!private_party_requested)
				{
					if (!game::virtual_lobby_loaded())
					{
						return scheduler::cond_continue;
					}

					console::info("Virtual lobby initialized.\n");
					if (party::dedicated_private_party_ready())
					{
						game::Cbuf_AddText(0, "map mp_shipment_s2 undead\n");
						return scheduler::cond_end;
					}

					private_party_requested = true;
					private_party_start_time = std::chrono::steady_clock::now();
					game::Cbuf_AddText(0, "xstartprivateparty\n");
					console::info("Waiting for the online private party to initialize...\n");
					return scheduler::cond_continue;
				}

				if (party::dedicated_private_party_ready())
				{
					game::Cbuf_AddText(0, "map mp_shipment_s2 undead\n");
					return scheduler::cond_end;
				}

				if (std::chrono::steady_clock::now() - private_party_start_time >= 30s)
				{
					console::error("Dedicated party: online private-party creation timed out.\n");
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
			disable_p2p_auth_ticket_validation();

			// Bypass the gamestate guard
			utils::hook::nop(0xF44F3_g, 6);

			// TODO: Add killserver only after a safe S2 shutdown wrapper or flow is confirmed.
			scheduler::once(run_startup, scheduler::pipeline::main, 1s);
		}
	};
}

REGISTER_COMPONENT(dedicated::component)
