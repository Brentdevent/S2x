#pragma once

#include "loot_catalog.hpp"

namespace demonware::loot_compatibility
{
	using loot_catalog::supply_drop;
	using loot_catalog::supply_drop_slot;

	struct mp_supply_drop_tier
	{
		int type{};
		std::string_view backend_id{};
		std::uint32_t item_id{};
		int rarity_floor{};
	};

	// Native tier variants differ only in the first card's rarity floor
	constexpr std::array<mp_supply_drop_tier, 3> mp_supply_drop_tiers
	{{
		{1, "sd_mp_rare", 2, 2},
		{14, "sd_mp_legendary", 74, 3},
		{13, "sd_mp_epic", 73, 4},
	}};

	struct mp_event_drop
	{
		int type{};
		std::string_view backend_id{};
		std::uint32_t item_id{};
		std::string_view operation{};
		bool bribe{};
	};

	// Exact native supplyDropTypes.csv shapes: event drops guarantee one event
	// card; these two bribes guarantee three distinct, unowned event cards.
	constexpr std::array<mp_event_drop, 4> mp_event_drops
	{{
		{15, "sd_mp_winter", 75, "winter", false},
		{16, "sd_mp_winter_bribe", 76, "winter", true},
		{17, "sd_mp_resist", 77, "resistance", false},
		{18, "sd_mp_resist_bribe", 78, "resistance", true},
	}};

	inline bool is_confirmed_mp_supply_drop(const supply_drop& drop)
	{
		const supply_drop_slot unrestricted{};

		if (drop.contains_zm_consumables)
		{
			return false;
		}

		for (const auto& event : mp_event_drops)
		{
			if (drop.type == event.type && drop.backend_id == event.backend_id && drop.item_id == event.item_id)
			{
				const supply_drop_slot restricted{0, 0, std::string{event.operation}, event.bribe};
				const auto other = event.bribe ? restricted : unrestricted;
				return drop.slots == std::array{restricted, other, other};
			}
		}

		if (drop.slots[1] != unrestricted || drop.slots[2] != unrestricted)
		{
			return false;
		}

		if (drop.type == 0 && drop.backend_id == "sd_mp" && drop.item_id == 1)
		{
			return drop.slots[0] == unrestricted;
		}

		for (const auto& tier : mp_supply_drop_tiers)
		{
			if (drop.type == tier.type && drop.backend_id == tier.backend_id && drop.item_id == tier.item_id)
			{
				return drop.slots[0] == supply_drop_slot{tier.rarity_floor, 0, {}, false};
			}
		}

		return false;
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
