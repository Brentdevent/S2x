#pragma once

#include "marketplace_store.hpp"
#include <rapidjson/document.h>
#include <string_view>

namespace demonware::reward_json
{
	bool bounded_ascii(std::string_view value, std::size_t maximum, bool allow_space = false);
	bool client_tx(const rapidjson::Value& value, std::string& result, bool exact_length = true);
	bool unique_members(const rapidjson::Value& object);
	bool common_fields(const rapidjson::Value& object, std::string_view action,
		std::string& transaction, bool exact_transaction_length = true);
	void add_string(rapidjson::Value& object, const char* name, std::string_view value,
		rapidjson::Document::AllocatorType& allocator);
	void add_detailed_inventory(rapidjson::Value& array,
		const marketplace_store::inventory_record& record,
		rapidjson::Document::AllocatorType& allocator);
	std::uint32_t modification_time();
}
