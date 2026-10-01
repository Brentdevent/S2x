#pragma once

#include "game/demonware/reward_game_event.hpp"

namespace zombies_progression
{
	// False leaves the native queue entry unacknowledged so it is retried
	bool process(const demonware::reward_game_events::event& event, std::uint64_t user);

	// Main pipeline only
	bool unlock_quests();
}
