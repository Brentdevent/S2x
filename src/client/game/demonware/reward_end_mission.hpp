#pragma once

#include "reward_action.hpp"

namespace demonware::reward_end_mission
{
	inline constexpr std::string_view action = "end_mission";

	reward::action_result handle(std::string_view json);
}
