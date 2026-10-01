#pragma once

#include "reward_game_event.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demonware::challenge_service
{
	struct action_result
	{
		std::string response{};
		std::vector<std::string> pushes{};
	};

	struct contract_sku
	{
		std::uint32_t sku_id{};
		std::string sku_data{};
		std::uint32_t price{};
		std::uint32_t item_id{};
	};

	bool load();
	bool handles_action(std::string_view action);
	std::vector<contract_sku> get_contract_skus();
	std::optional<action_result> handle_action(std::string_view action, std::string_view client_transaction,
		const rapidjson::Value& request);
	std::vector<std::string> handle_game_event(const reward_game_events::event& event);
}
