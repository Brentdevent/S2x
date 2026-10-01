#pragma once

#include "achievement_store.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace demonware::achievement_orders
{
	inline constexpr std::string_view activation_action = "activate_scheduled_user_achievement";

	inline constexpr std::string_view contract_action = "activate_user_contract";

	std::optional<achievement_store::order_offer> contract_for_token(std::uint32_t token, std::uint64_t timestamp);

	struct activation_response
	{
		std::uint32_t error{};
		std::string json;
	};

	std::optional<std::string> scheduled_response(std::string_view transaction, std::uint64_t timestamp);
	activation_response activate(std::string_view request, std::uint64_t user_id, std::uint64_t timestamp);
}
