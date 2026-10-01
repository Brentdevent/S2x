#pragma once

#include "loot_catalog.hpp"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace demonware::collection_catalog
{
	struct collection
	{
		std::uint32_t id{};
		std::uint32_t reward{};
		std::vector<std::uint32_t> items;
		std::string rule;
	};

	// Definitions only. Ownership and completion remain ordinary inventory.
	struct catalog
	{
		std::shared_ptr<const loot_catalog::catalog> source;
		std::vector<collection> collections;
		std::unordered_set<std::uint32_t> purchasable_items;
	};

	std::string rule_id(std::uint32_t collection_id);
	std::string rule_id(const std::string& name);
	std::shared_ptr<const catalog> get_snapshot();
	void publish(catalog value);
}
