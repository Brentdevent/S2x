#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/zombies_inventory.hpp"

#include <utils/hook.hpp>

namespace unlimited_zombies_consumables
{
	namespace
	{
		constexpr int unlimited_consumable_quantity = 999;

		const game::dvar_t* cg_unlimited_zm_consumables{};
		utils::hook::detour get_item_quantity_hook;

		int get_item_quantity_stub(const unsigned int controller_index, const unsigned int item_guid)
		{
			if (cg_unlimited_zm_consumables && cg_unlimited_zm_consumables->current.enabled &&
				!game::zombies_inventory::is_progression_item(item_guid) &&
				game::Inventory_IsItemGuidAZMConsumable(item_guid))
			{
				return unlimited_consumable_quantity;
			}

			return get_item_quantity_hook.invoke<int>(controller_index, item_guid);
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated() || !game::environment::is_zombies())
			{
				return;
			}

			cg_unlimited_zm_consumables = game::Dvar_RegisterBool(
				"cg_unlimited_zm_consumables", false, game::DVAR_FLAG_SAVED);

			get_item_quantity_hook.create(game::Inventory_GetItemQuantity, get_item_quantity_stub);
		}
	};
}

REGISTER_COMPONENT(unlimited_zombies_consumables::component)
