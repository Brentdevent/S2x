#pragma once

#include "store.hpp"

#include <map>

namespace demonware::marketplace_store::detail
{
	// Private owned state shared by staged transaction semantics and persistence.
	// Only marketplace_store publishes this state, under its existing mutex.
	struct state
	{
		std::string achievement_state{R"({"achievements":[],"orderActivations":[]})"};
		std::map<std::uint8_t, std::uint32_t> currencies{};
		std::map<std::uint32_t, inventory_record> inventory{};
		std::map<std::string, committed_economy_transaction> processed_transactions{};
		std::uint64_t next_transaction_sequence{1};
	};

	// The same record invariants apply to new edits and decoded disk records.
	bool is_valid_achievement_state(const std::string& json);
	bool is_safe_text(const std::string& value, std::size_t maximum_length, bool allow_space);
	bool is_valid_inventory_record(const inventory_record& record);
	bool is_valid_response_json(const std::string& response);

	enum class save_result
	{
		saved,
		too_large,
		io_error,
	};

	namespace persistence
	{
		// Existing economy files use only the current schema and are never rewritten
		// on load. Errors leave the destination state and original file alone.
		store_status load(state* result);
		save_result save(const state& value);
	}
}
