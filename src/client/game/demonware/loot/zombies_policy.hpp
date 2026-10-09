#pragma once

#include "catalog.hpp"

namespace demonware::marketplace_store { class transaction; }

namespace demonware::zombies_loot_policy
{
	bool supports(const loot_catalog::supply_drop& drop);
	std::optional<std::vector<loot_catalog::loot_item>> select(
		const loot_catalog::catalog& source, const loot_catalog::supply_drop& drop,
		const marketplace_store::transaction& transaction, std::uint64_t random_state, float rarity_scale = 1.0f);
}
