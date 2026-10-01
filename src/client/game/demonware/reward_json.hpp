#pragma once

#include "marketplace_store.hpp"

namespace demonware::reward_json
{
	bool bounded_ascii(std::string_view value, std::size_t maximum, bool allow_space = false);
	bool client_tx(const rapidjson::Value& value, std::string& result, bool exact_length = true);
	bool unique_members(const rapidjson::Value& object);
	const rapidjson::Value* find_member(const rapidjson::Value& object, const char* name);
	std::string_view view(const rapidjson::Value& value);
	bool equals(const rapidjson::Value* value, std::string_view text);
	bool ascii_string(const rapidjson::Value* value, std::size_t maximum);
	bool common_fields(const rapidjson::Value& object, std::string_view action,
		std::string& transaction, bool exact_transaction_length = true);
	void add_string(rapidjson::Value& object, const char* name, std::string_view value,
		rapidjson::Document::AllocatorType& allocator);
	rapidjson::Value inventory_row(const marketplace_store::inventory_record& record,
		rapidjson::Document::AllocatorType& allocator);
	void add_detailed_inventory(rapidjson::Value& array, const marketplace_store::inventory_record& record,
		rapidjson::Document::AllocatorType& allocator);
	std::string encode(const rapidjson::Value& value);
	std::uint32_t modification_time();
}
