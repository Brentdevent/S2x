#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "console/console.hpp"
#include "network.hpp"

#include <utils/hook.hpp>

namespace patches
{
	namespace
	{
		
	}

	class component final : public multiplayer_component
	{
	public:
		void post_thread_setup() override
		{
#ifdef DEBUG
			// Allow multiple instances for testing purposes.
			utils::hook::set(0x78A5F0_g, 0xC301B0);
#endif
		}
	};
}

REGISTER_COMPONENT(patches::component)
