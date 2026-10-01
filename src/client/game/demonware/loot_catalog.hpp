#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace demonware::loot_catalog
{
	struct drop_type
	{
		std::uint32_t guid{};
		int min_rarity{};
		int focus{};
		bool zombies{};
	};

	bool load();
	bool is_loaded();

	std::optional<drop_type> find_drop(std::string_view name);
	std::optional<std::uint32_t> find_drop_guid(std::string_view name);
	std::vector<std::uint32_t> roll_drop(const drop_type& drop, std::size_t count);
}
