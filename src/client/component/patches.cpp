#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "console/console.hpp"

#include <utils/hook.hpp>

namespace patches
{
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
		}
	};
}

REGISTER_COMPONENT(patches::component)
