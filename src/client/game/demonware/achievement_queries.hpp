#pragma once
#include "reward_action.hpp"
#include <string_view>

namespace demonware::achievement_queries
{
	reward::action_result handle(std::string_view action, std::string_view json);
}
