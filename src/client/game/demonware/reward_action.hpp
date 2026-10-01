#pragma once

#include "marketplace_store.hpp"
#include <cstdint>
#include <string>

namespace demonware::reward
{
	struct action_result
	{
		std::uint32_t error{};
		std::string response_json{};
		marketplace_store::transaction_status transaction_status{
			marketplace_store::transaction_status::invalid_argument};
	};

	// Conservative local mapping, NOT a captured S2 negative-path contract.
	std::uint32_t provisional_transaction_error(marketplace_store::transaction_status status);
	action_result transaction_response(marketplace_store::transaction_result transaction);
}
