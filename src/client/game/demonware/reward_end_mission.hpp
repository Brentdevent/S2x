#pragma once

#include "reward_action.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace demonware::reward_end_mission
{
	inline constexpr std::string_view action = "end_mission";
	inline constexpr std::size_t maximum_json_length = 6144 - 1;
	inline constexpr std::size_t client_tx_length = 24;
	inline constexpr std::size_t maximum_usage_exclusions = 5;

	struct request
	{
		std::string client_tx{};
		std::int32_t time_played{};
		bool active_time{};
		std::string game_mode{};
		std::string sub_game_mode{};
		std::vector<std::int32_t> usage_exclusions{};
		bool has_report_usage_time{};
		std::uint32_t mission_instance_id{};
		std::string match_id{};
	};

	bool parse_request(std::string_view json, request& result);
	reward::action_result handle(std::string_view json);
}
