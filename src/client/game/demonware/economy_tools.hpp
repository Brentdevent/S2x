#pragma once

#include "marketplace_store.hpp"
#include <string_view>

namespace demonware::economy_tools
{
	bool parse_number(std::string_view text, std::uint32_t& value);
	const char* currency_name(std::uint32_t id);
	marketplace_store::transaction_result give_item(std::uint32_t id, std::uint32_t amount,
		std::uint64_t user, const std::string& transaction);
	marketplace_store::transaction_result give_currency(std::uint32_t id, std::uint32_t amount,
		std::uint64_t user, const std::string& transaction);
	bool succeeded(marketplace_store::transaction_status status);
}
