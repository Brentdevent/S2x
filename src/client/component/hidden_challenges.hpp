#pragma once

#include <cstdint>

namespace demonware::reward_game_events
{
	struct event;
}

namespace hidden_challenges
{
	bool get_completion(const demonware::reward_game_events::event& event,
		std::uint32_t& group, std::uint32_t& challenge);
	void submit_completion(std::uint32_t group, std::uint32_t challenge);
	void submit_reward_game_event(demonware::reward_game_events::event event);
}
