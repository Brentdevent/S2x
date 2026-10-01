#pragma once

#include "game/demonware/reward_game_event.hpp"

namespace zombies_progression
{
	// False leaves the local native queue entry unacknowledged for retry.
	bool process(const demonware::reward_game_events::event& event, std::uint64_t user);
	// Main pipeline only; does not change rank, character challenges or native DDL.
	bool unlock_quests();
}
