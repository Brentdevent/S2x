#pragma once

#include "game/demonware/achievement/store.hpp"
#include "game/demonware/loot/catalog.hpp"
#include "game_event.hpp"

namespace demonware::hq_rewards
{
	achievement_record payroll(bool master_prestige);
	std::vector<achievement_record> initial_records();

	// An empty push ignores the event, false retries the native queue entry with nothing committed
	bool process(const reward_game_events::event& event, std::uint64_t user,
		std::uint32_t timestamp, const loot_catalog::catalog* catalog, std::string& push);
}
