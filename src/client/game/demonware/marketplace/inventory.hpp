#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "store.hpp"

namespace demonware
{
	class byte_buffer;
}

namespace demonware::marketplace_inventory
{
	std::vector<std::string> item_updates(const std::vector<marketplace_store::inventory_record>& changed,
		std::uint64_t local_user_id);

	// The empty acknowledgement must be followed by the persisted 0x31 pushes.
	// Native inventory updates release the task's pending-push flag.
	std::uint32_t consume(byte_buffer* buffer, std::uint64_t local_user_id,
		std::vector<std::string>& updates);

	// Persist the opaque native item data only; never grant or replace an item.
	std::uint32_t put_item_data(byte_buffer* buffer, std::uint64_t local_user_id);
}
