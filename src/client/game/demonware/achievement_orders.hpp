#pragma once

#include "achievement_store.hpp"

namespace demonware::achievement_orders
{
	constexpr std::string_view activation_action = "activate_scheduled_user_achievement";
	constexpr std::string_view deactivation_action = "deactivate_user_achievement";
	constexpr std::string_view contract_action = "activate_user_contract";

	struct activation_response
	{
		std::uint32_t error{};
		std::string json{};
	};

	std::optional<achievement_store::order_offer> contract_for_token(std::uint32_t token, std::uint64_t timestamp);
	std::optional<std::string> scheduled_response(std::string_view transaction, std::uint64_t timestamp);
	activation_response deactivate(std::string_view json, std::uint64_t user_id);
	activation_response activate(std::string_view json, std::uint64_t user_id, std::uint64_t timestamp);
}
