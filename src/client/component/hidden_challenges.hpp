#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hidden_challenges
{
	struct reward_event_parameter
	{
		std::string selector{};
		std::uint64_t value{};
	};

	struct reward_game_event
	{
		std::string name{};
		std::int64_t timestamp{};
		std::vector<reward_event_parameter> parameters{};
	};

	void submit_reward_game_event(reward_game_event event);
}
