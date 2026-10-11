#pragma once

#include "game/demonware/marketplace/store.hpp"

namespace demonware::reward
{
	struct action_result
	{
		std::uint32_t error{};
		std::string response_json{};
		marketplace_store::transaction_status transaction_status{marketplace_store::transaction_status::invalid_argument};
	};

	std::uint32_t transaction_error(marketplace_store::transaction_status status);
	action_result transaction_response(marketplace_store::transaction_result transaction);
}
