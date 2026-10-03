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
			if (game::environment::is_store_native())
			{
				utils::hook::jump(0x140716192_ms, 0x1407162AB_ms);
			}
			else
			{
				utils::hook::set(game::select(0x78A5F0, 0x0), 0xC301B0);
			}
		}

		void post_unpack() override
		{          
			// Skip intro's
			game::Dvar_RegisterBool("2665", true, game::DVAR_FLAG_NONE);   

			validate_fastfile_checksums_hook.create(game::select(0xF7F90, 0xD6B40), validate_fastfile_checksums_stub);

			// unlock safeArea_*
			utils::hook::jump(game::select(0x46E271, 0x3FA21E), game::select(0x46E2B7, 0x3FA264));
			dvars::override::register_float("safeArea_adjusted_horizontal", 1.0f, 0.0f, 1.0f, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("safeArea_adjusted_vertical", 1.0f, 0.0f, 1.0f, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("safeArea_horizontal", 1.0f, 0.0f, 1.0f, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("safeArea_vertical", 1.0f, 0.0f, 1.0f, game::DVAR_FLAG_SAVED);
		}
	};
}

REGISTER_COMPONENT(patches::component)
