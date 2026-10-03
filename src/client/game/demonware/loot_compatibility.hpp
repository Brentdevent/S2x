#pragma once

#include "loot_catalog.hpp"

namespace demonware::loot_compatibility
{
	using loot_catalog::supply_drop;
	using loot_catalog::supply_drop_slot;

	inline std::optional<int> operation_index(const std::string_view operation)
	{
		// Stock InventoryOperations / StatsTable.Operation.
		constexpr std::array names{"overlord", "winter", "resistance", "escalation", "confrontation",
			"liberation", "special", "undead", "summer", "halloween", "season2", "season2part2", "season2part3"};
		const auto found = std::ranges::find(names, operation);
		if (found == names.end())
		{
			return std::nullopt;
		}

		return static_cast<int>(found - names.begin());
	}

	inline bool is_confirmed_mp_supply_drop(const supply_drop& drop)
	{
		// The native table owns slot composition. Membership/odds remain S2x
		// policy; new slot types must be understood before they can be opened.
		const auto legendary = drop.type == 14 && drop.item_id == 74 && drop.backend_id == "sd_mp_legendary" &&
			drop.slots == std::array{supply_drop_slot{3, 0, {}, false}, supply_drop_slot{}, supply_drop_slot{}};
		if (drop.contains_zm_consumables || !drop.backend_id.starts_with("sd_mp") ||
			(!drop.menu_available && !legendary))
		{
			return false;
		}

		return std::ranges::all_of(drop.slots, [](const supply_drop_slot& slot)
		{
			const auto type_supported = (slot.type >= 0 && slot.type <= 22) || slot.type == 25 || slot.type == 26;
			return type_supported && slot.rarity >= 0 && slot.rarity <= 5 &&
				(slot.operation.empty() || operation_index(slot.operation).has_value());
		});
	}

	// Inventory_IsItemGuidHidden formats an unpadded lowercase "0x%x" key and compares it case-insensitively
	inline bool matches_stock_item_guid_key(const std::string_view value, const std::uint32_t item_guid)
	{
		if (!item_guid)
		{
			return false;
		}

		std::array<char, 10> formatted{'0', 'x'};
		const auto result = std::to_chars(formatted.data() + 2, formatted.data() + formatted.size(), item_guid, 16);
		if (result.ec != std::errc{})
		{
			return false;
		}

		const std::string_view expected{formatted.data(), result.ptr};
		return std::ranges::equal(value, expected, [](const char left, const char right)
		{
			return std::tolower(static_cast<unsigned char>(left)) == right;
		});
	}

	// Mirrors the final atoi(cell) > 0 test at S2 MP 0x27525D
	inline bool stock_hidden_item_cell_is_true(const std::string_view value)
	{
		const std::string bounded{value};
		return std::atoi(bounded.data()) > 0;
	}

	inline bool stock_internal_costume_component(const std::uint32_t item_guid)
	{
		const auto subtype = (item_guid >> 20) & 0xF;
		return item_guid && (item_guid & 0x07000000) == 0x06000000 && subtype != 0 && subtype != 8 &&
			(item_guid & 0x100) == 0;
	}

	inline bool stock_azm_consumable(const std::uint32_t item_guid)
	{
		return item_guid && (item_guid & 0x07000000) == 0x04000000 && ((item_guid >> 21) & 7) == 5;
	}
}
