#pragma once

#include "achievement_store.hpp"
#include "loot_catalog.hpp"
#include "reward_game_event.hpp"

namespace demonware::hq_rewards
{
	achievement_record payroll(bool master_prestige);
	std::vector<achievement_record> initial_records();
	// Empty push means an unrelated/invalid occurrence. False means retry the
	// native queue entry; nothing from this reward has been committed.
	bool process(const reward_game_events::event& event, std::uint64_t user,
		std::uint32_t timestamp, const loot_catalog::catalog* catalog, std::string& push);
}
