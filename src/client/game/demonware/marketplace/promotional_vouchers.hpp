#pragma once

#include "collection.hpp"
#include <optional>

namespace demonware::promotional_vouchers
{
	// Local offline delivery policy. Inventory vouchers drive the stock HQ Post.
	bool deliver(std::uint64_t user);
	std::optional<marketplace_collection::result> redeem(
		const marketplace_collection::request& input, std::uint64_t user);
}
