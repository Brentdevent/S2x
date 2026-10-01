#pragma once

#include "loot_catalog.hpp"
#include "reward_action.hpp"

namespace demonware::reward_supply_drop
{
	inline constexpr std::string_view action = "open_supply_drop";

	reward::action_result handle(std::string_view json, std::uint64_t user_id,
		const std::shared_ptr<const loot_catalog::catalog>& catalog, std::uint32_t modification_time,
		float rarity_scale = 1.0f);
}
