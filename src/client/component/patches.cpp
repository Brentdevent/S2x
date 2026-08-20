#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "console/console.hpp"

#include <utils/hook.hpp>

namespace patches
{
	namespace
	{
		utils::hook::detour validate_fastfile_checksums_hook;

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
		}
	};
}

REGISTER_COMPONENT(patches::component)
