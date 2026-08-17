#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>

namespace aim_assist
{
	namespace
	{
		game::dvar_t* register_allow_aim_assist_stub(const char* name, const bool,
			const game::DvarFlags flags)
		{
			const auto replicated_flags = static_cast<game::DvarFlags>(
				flags | game::DVAR_FLAG_REPLICATED);

			return game::Dvar_RegisterBool(name, true, replicated_flags);
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::is_multiplayer())
			{
				return;
			}

			// Stock dvar 387 already gates slowdown and lock-on in Multiplayer.
			// Enable it by default and replicate the server's value to clients.
			utils::hook::call(0x5DE6EC_g, register_allow_aim_assist_stub);
		}
	};
}

REGISTER_COMPONENT(aim_assist::component)
