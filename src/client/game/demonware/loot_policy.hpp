#pragma once

#include "loot_catalog.hpp"

namespace demonware::marketplace_store { class transaction; }

namespace demonware::loot_policy
{
	// Explicit local MP membership/weights, not recovered retail odds. Native
	// transaction, inventory and duplicate conversion behavior live elsewhere.
	std::vector<loot_catalog::loot_item> eligible_items(const loot_catalog::catalog& source,
		const loot_catalog::supply_drop& drop, const marketplace_store::transaction& transaction);
	std::vector<loot_catalog::loot_item> rule_candidates(const loot_catalog::catalog& source,
		const loot_catalog::supply_drop* drop = nullptr);
	std::optional<std::vector<loot_catalog::loot_item>> select_supply_drop_items(
		const loot_catalog::supply_drop& drop, std::vector<loot_catalog::loot_item> candidates,
		std::uint64_t random_state, float rarity_scale = 1.0f);
	std::array<unsigned, 5> rarity_weights(bool common, int floor, float scale);
	std::optional<std::size_t> select_card(const std::vector<loot_catalog::loot_item>& candidates,
		const std::array<unsigned, 5>& weights, std::uint64_t& random_state);
}
