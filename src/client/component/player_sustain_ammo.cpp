#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>

namespace player_sustain_ammo
{
	namespace
	{
		const game::dvar_t* player_sustain_ammo{};

		utils::hook::detour consume_ammo_hook;
		utils::hook::detour consume_reserve_ammo_hook;

		void consume_ammo_stub(void* player_state, const game::Weapon* weapon,
			const bool alternate, const int amount, const int hand)
		{
			if (player_sustain_ammo && player_sustain_ammo->current.enabled)
			{
				return;
			}

			consume_ammo_hook.invoke<void>(player_state, weapon, alternate, amount, hand);
		}

		void consume_reserve_ammo_stub(void* player_state, const game::Weapon* weapon,
			const bool alternate, const int amount, const int hand)
		{
			if (player_sustain_ammo && player_sustain_ammo->current.enabled)
			{
				return;
			}

			consume_reserve_ammo_hook.invoke<void>(player_state, weapon, alternate, amount, hand);
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			player_sustain_ammo = game::Dvar_RegisterBool("player_sustainAmmo", false,
				static_cast<game::DvarFlags>(game::DVAR_FLAG_CHEAT | game::DVAR_FLAG_REPLICATED));

			consume_ammo_hook.create(game::select(0x3B4C90, 0x5761E0), consume_ammo_stub);

			if (game::environment::uses_multiplayer_binary())
			{
				consume_reserve_ammo_hook.create(0x3B4A60_g, consume_reserve_ammo_stub);
			}
		}
	};
}

REGISTER_COMPONENT(player_sustain_ammo::component)
