#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "console/console.hpp"
#include "dvars.hpp"

#include <utils/hook.hpp>

namespace patches
{
	namespace
	{
		utils::hook::detour validate_fastfile_checksums_hook;

		void lobby_client_state_stub(utils::hook::assembler& a)
		{
			const auto no_client = a.new_label();

			// EDI is a session slot (0..47), which may exceed the client allocation.
			// Keep the full session walk and guard only the server client lookup.
			a.mov(rax, reinterpret_cast<size_t>(game::sv_maxclients.get()));
			a.cmp(edi, dword_ptr(rax));
			a.jge(no_client);
			a.mov(rax, reinterpret_cast<size_t>(game::mp::svs_clients.get()));
			a.mov(rax, qword_ptr(rax));
			a.test(rax, rax);
			a.jz(no_client);
			a.jmp(0x19A6D_g); // Original client-state comparison.

			a.bind(no_client);
			a.jmp(0x19A73_g); // Continue processing the session member.
		}

		void validate_fastfile_checksums_stub(game::mp::client_t* client)
		{
			const auto previous_pure_state = client->pureAuthentic;

			validate_fastfile_checksums_hook.invoke<void>(client);

			// Steam and Microsoft Store fastfiles use different signatures, causing stock ffcs
			// to falsely mark otherwise compatible clients as impure.
			if (previous_pure_state != 2 && client->pureAuthentic == 2)
			{
				client->pureAuthentic = 1;
			}
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_thread_setup() override
		{
			// Intentionally allow multiple clients and dedicated servers in every build and mode.
			utils::hook::set(0x78A5F0_g, 0xC301B0);
		}

		void post_unpack() override
		{          
			// Skip intro's
			game::Dvar_RegisterBool("2665", true, game::DVAR_FLAG_NONE);   

			validate_fastfile_checksums_hook.create(0xF7F90_g, validate_fastfile_checksums_stub);

			utils::hook::nop(0x19A66_g, 7);
			utils::hook::jump(0x19A66_g, utils::hook::assemble(lobby_client_state_stub));

			// unlock safeArea_*
			utils::hook::jump(0x46E271_g, 0x46E2B7_g);
			dvars::override::register_float("safeArea_adjusted_horizontal", 1.0f, 0.0f, 1.0f, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("safeArea_adjusted_vertical", 1.0f, 0.0f, 1.0f, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("safeArea_horizontal", 1.0f, 0.0f, 1.0f, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("safeArea_vertical", 1.0f, 0.0f, 1.0f, game::DVAR_FLAG_SAVED);
		}
	};
}

REGISTER_COMPONENT(patches::component)
