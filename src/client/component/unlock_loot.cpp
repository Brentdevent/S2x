#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "component/console/console.hpp"
#include "game/zombies_inventory.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>

namespace unlock_loot
{
	namespace
	{
		const game::dvar_t* cg_unlock_all_loot{};

		utils::hook::detour is_loot_item_unlocked_hook;
		std::atomic_bool progression_override_reported{};

		bool is_loot_item_unlocked_stub(const unsigned int item_id)
		{
			if (cg_unlock_all_loot && cg_unlock_all_loot->current.enabled)
			{
				if (game::zombies_inventory::is_progression_item(item_id))
				{
					if (!progression_override_reported.exchange(true))
					{
						console::debug("[unlock_loot] preserving Zombies progression-item ownership\n");
					}

					return is_loot_item_unlocked_hook.invoke<bool>(item_id);
				}

				return true;
			}

			return is_loot_item_unlocked_hook.invoke<bool>(item_id);
		}

		bool loot_item_unlocked()
		{
			return true;
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated())
			{
				utils::hook::jump(0xD0980_g, loot_item_unlocked);
				return;
			}

			cg_unlock_all_loot = game::Dvar_RegisterBool("cg_unlockall_loot", false, game::DVAR_FLAG_SAVED);
			is_loot_item_unlocked_hook.create(0xD0980_g, is_loot_item_unlocked_stub);
		}
	};
}

REGISTER_COMPONENT(unlock_loot::component)
