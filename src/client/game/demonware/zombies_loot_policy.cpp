#include <std_include.hpp>

#include "zombies_loot_policy.hpp"
#include "loot_policy.hpp"
#include "supply_drop_inventory.hpp"

namespace demonware::zombies_loot_policy
{
	using namespace loot_catalog;

	namespace
	{
		std::vector<loot_item> consumables(const catalog& source,
			const marketplace_store::transaction& transaction)
		{
			std::vector<loot_item> result;
			for (const auto& row : source.items)
			{
				const auto& item = row.item;
				// LOCAL membership: playable definitions from zombieConsumablesTable,
				// joined to their exact StatsTable GUID/rarity/presentation. No name
				// suffix arithmetic: later consumables use different GUID layouts.
				if (!row.zombie_consumable_available || item.group != "zombieconsumable" ||
					!item.azm_consumable || !item.item_id || item.item_id > INT32_MAX ||
					!row.loot_flag || !row.rarity_eligible || row.challenge || row.entitlement ||
					!row.reference_valid || !row.presentation_available || !row.rarity_valid ||
					item.rarity < 0 || item.rarity > 3 || item.stock_hidden_item ||
					item.stock_internal_costume_component)
				{
					continue;
				}
				if (const auto owned = transaction.get_inventory(item.item_id);
					owned && !supply_drop_inventory::can_stack(*owned))
				{
					continue;
				}
				result.push_back(item);
			}
			std::ranges::sort(result, {}, &loot_item::item_id);
			result.erase(std::unique(result.begin(), result.end(), [](const auto& a, const auto& b)
				{ return a.item_id == b.item_id; }), result.end());
			return result;
		}
	}

	bool supports(const supply_drop& drop)
	{
		const supply_drop_slot any{}, pack{0, 24, {}, false}, rare{2, 0, {}, false};
		const supply_drop_slot epic{4, 0, {}, false};
		const supply_drop_slot consumable{0, 23, {}, false}, rare_consumable{2, 23, {}, false};
		// Exact native supplyDropTypes.csv descriptors. Slot type 24 expands to
		// three cards; it is not booster-pack type 24 (an unrelated MP bribe).
		if (drop.type == 2 && drop.backend_id == "sd_zombie" && drop.item_id == 5)
		{
			return drop.contains_zm_consumables && drop.slots == std::array{pack, any, any};
		}
		if (drop.type == 3 && drop.backend_id == "sd_zombie_rare" && drop.item_id == 6)
		{
			return drop.contains_zm_consumables && drop.slots == std::array{rare, pack, any};
		}
		if (drop.type == 19 && drop.backend_id == "sd_zombie_epic" && drop.item_id == 79)
		{
			return drop.contains_zm_consumables && drop.slots == std::array{epic, pack, any};
		}
		return drop.type == 62 && drop.backend_id == "sd_zombie_consumables" && drop.item_id == 122 &&
			!drop.contains_zm_consumables && drop.slots == std::array{rare_consumable, consumable, consumable};
	}

	std::optional<std::vector<loot_item>> select(const catalog& source, const supply_drop& drop,
		const marketplace_store::transaction& transaction, std::uint64_t random_state, const float rarity_scale)
	{
		if (!supports(drop))
		{
			return std::nullopt;
		}
		auto consumable_pool = consumables(source, transaction);
		std::vector<loot_item> cosmetic_pool;
		if (drop.type != 62)
		{
			const auto mp = find_supply_drop(source, "sd_mp");
			if (!mp)
			{
				return std::nullopt;
			}
			// Reuse the existing local MP collectible/duplicate eligibility only.
			// Slot composition remains specific to Zombies.
			cosmetic_pool = loot_policy::eligible_items(source, *mp, transaction);
		}
		std::vector<loot_item> result;
		for (const auto& slot : drop.slots)
		{
			auto& pool = slot.type ? consumable_pool : cosmetic_pool;
			const auto count = slot.type == 24 ? 3 : 1;
			const auto weights = loot_policy::rarity_weights(drop.type == 2, slot.rarity, rarity_scale);
			for (int card = 0; card < count; ++card)
			{
				const auto index = loot_policy::select_card(pool, weights, random_state);
				if (!index)
				{
					return std::nullopt;
				}
				result.push_back(pool[*index]);
				// Consumables repeat/stack once per exact rarity card; cosmetics stay
				// distinct within the pack and use the existing native pawn path.
				if (!slot.type)
				{
					pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(*index));
				}
			}
		}
		return result;
	}
}
