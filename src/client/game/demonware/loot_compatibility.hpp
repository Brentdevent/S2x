#pragma once

#include "loot_catalog.hpp"

#include <cstdlib>

namespace demonware::loot_compatibility
{
	using loot_catalog::supply_drop;
	using loot_catalog::supply_drop_slot;

	inline bool is_confirmed_mp_supply_drop(const supply_drop& drop)
	{
		const supply_drop_slot unrestricted{};

		if (drop.contains_zm_consumables || drop.slots[1] != unrestricted ||
			drop.slots[2] != unrestricted)
		{
			return false;
		}

		if (drop.type == 0 && drop.backend_id == "sd_mp" && drop.item_id == 1)
		{
			return drop.slots[0] == unrestricted;
		}

		// Native tier variants differ only in the first card's rarity floor.
		const auto floor = drop.type == 1 && drop.backend_id == "sd_mp_rare" && drop.item_id == 2 ? 2 :
			drop.type == 14 && drop.backend_id == "sd_mp_legendary" && drop.item_id == 74 ? 3 :
			drop.type == 13 && drop.backend_id == "sd_mp_epic" && drop.item_id == 73 ? 4 : 0;
		return floor && drop.slots[0] == supply_drop_slot{floor, 0, {}, false};
	}

	inline bool matches_stock_item_guid_key(const std::string_view value,
		const std::uint32_t item_guid)
	{
		// Inventory_IsItemGuidHidden formats the key as lowercase, unpadded
		// "0x%x", then performs a case-insensitive column-18 lookup.
		if (!item_guid)
		{
			return false;
		}

		std::array<char, 10> formatted{};
		formatted[0] = '0';
		formatted[1] = 'x';
		const auto result = std::to_chars(formatted.data() + 2,
			formatted.data() + formatted.size(), item_guid, 16);
		if (result.ec != std::errc{})
		{
			return false;
		}

		const auto formatted_size = static_cast<std::size_t>(result.ptr - formatted.data());
		if (value.size() != formatted_size)
		{
			return false;
		}

		for (std::size_t index = 0; index < formatted_size; ++index)
		{
			auto character = value[index];
			if (character >= 'A' && character <= 'Z')
			{
				character = static_cast<char>(character - 'A' + 'a');
			}
			if (character != formatted[index])
			{
				return false;
			}
		}

		return true;
	}

	inline bool stock_hidden_item_cell_is_true(const std::string_view value)
	{
		// This mirrors the final atoi(cell) > 0 test at S2 MP 0x27525D.
		const std::string bounded_value{value};
		return std::atoi(bounded_value.c_str()) > 0;
	}

	inline bool stock_internal_costume_component(const std::uint32_t item_guid)
	{
		const auto subtype = (item_guid >> 20) & 0xF;
		return item_guid && (item_guid & 0x07000000) == 0x06000000 && subtype != 0 &&
			subtype != 8 && (item_guid & 0x100) == 0;
	}

	inline bool stock_azm_consumable(const std::uint32_t item_guid)
	{
		return item_guid && (item_guid & 0x07000000) == 0x04000000 &&
			((item_guid >> 21) & 7) == 5;
	}

}
