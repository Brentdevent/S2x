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

			// enable dvar 387 & set replicated flags
			utils::hook::call(game::select(0x5DE6EC, 0x569E3C), register_allow_aim_assist_stub);
		}
	};
}

REGISTER_COMPONENT(aim_assist::component)
