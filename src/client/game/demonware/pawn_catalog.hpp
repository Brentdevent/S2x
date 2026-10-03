#pragma once

#include "loot_catalog.hpp"
#include <unordered_map>

namespace demonware::pawn_catalog
{
	struct item
	{
		std::uint32_t amount{};
		std::uint8_t currency{};
		// Empty for Task 199; the native any-division uniform queue uses 242.
		std::string rule;
	};

	struct catalog
	{
		std::shared_ptr<const loot_catalog::catalog> source;
		std::unordered_map<std::uint32_t, item> items;
	};

	std::shared_ptr<const catalog> get_snapshot();
	void publish(catalog value);
}
