#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace demonware::loot_catalog
{
	struct supply_drop_slot
	{
		int rarity{};
		int type{};
		std::string operation{};
		bool dupe_protection{};

		bool operator==(const supply_drop_slot&) const = default;
	};

	struct supply_drop
	{
		int type{};
		std::string backend_id{};
		std::uint32_t item_id{};
		bool contains_zm_consumables{};
		std::array<supply_drop_slot, 3> slots{};

		bool operator==(const supply_drop&) const = default;
	};

	struct loot_item
	{
		std::uint32_t item_id{};
		int rarity{};
		int collection_id{};
		std::optional<int> operation{};
		std::string group{};
		std::string reference{};
		std::string ignore{};
		std::string hidden_item{};
		std::string production_level{};
		bool azm_consumable{};
		bool stock_hidden_item{};
		bool stock_internal_costume_component{};
	};

	struct table_status
	{
		bool table_asset_available{};
		bool table_values_available{};
		bool table_dimensions_valid{};
		bool required_columns_available{};
		int table_row_count{};
		int table_column_count{};
	};


	// Owned row facts. Native parsing is separate from local selection policy.
	struct item_definition
	{
		loot_item item{};
		std::string display_name{};
		bool loot_flag{};
		bool rarity_eligible{};
		bool challenge{};
		bool entitlement{};
		bool collection_reward{};
		bool localized_name{};
		bool reference_valid{};
		bool presentation_available{};
		bool zombie_consumable_available{};
		bool rarity_valid{};
		bool collection_valid{true};
		bool operation_valid{true};
	};

	struct social_rank_reward
	{
		std::uint32_t threshold{}, item_id{}, quantity{};
	};

	struct cod_point_bundle
	{
		std::string id{}, title{};
		std::uint32_t amount{};
		std::string image{};
	};

	struct catalog
	{
		std::uint64_t generation{};
		std::uint32_t producer_thread_id{};
		std::vector<social_rank_reward> social_ranks{};
		std::vector<cod_point_bundle> cod_point_bundles{};
		std::vector<supply_drop> supply_drops{};
		std::vector<item_definition> items{};
		// Sorted native GUIDs for accessibility overrides, independent of drop pools.
		std::vector<std::uint32_t> permanent_customization{};
		table_status source_table{};
	};
	namespace detail
	{
		template <typename T>
		inline bool parse_integer(const std::string_view value, T& result)
		{
			if (value.empty())
			{
				return false;
			}

			const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
			return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
		}

		inline bool parse_optional_nonnegative(const std::string_view value, int& result)
		{
			result = 0;
			return value.empty() || (parse_integer(value, result) && result >= 0);
		}

		inline bool parse_optional_flag(const std::string_view value, bool& result)
		{
			result = false;
			if (value.empty() || value == "0")
			{
				return true;
			}

			if (value == "1")
			{
				result = true;
				return true;
			}

			return false;
		}
	}

	inline std::optional<supply_drop> parse_supply_drop_row(
		const std::span<const std::string_view> cells)
	{
		if (cells.size() <= 23 || cells[4].empty())
		{
			return std::nullopt;
		}

		supply_drop drop{};
		if (!detail::parse_integer(cells[1], drop.type) ||
			!detail::parse_integer(cells[5], drop.item_id) || !drop.item_id ||
			!detail::parse_optional_flag(cells[11], drop.contains_zm_consumables))
		{
			return std::nullopt;
		}

		drop.backend_id.assign(cells[4]);
		for (std::size_t slot_index = 0; slot_index < drop.slots.size(); ++slot_index)
		{
			const auto first_column = 12 + slot_index * 4;
			auto& slot = drop.slots[slot_index];
			if (!detail::parse_optional_nonnegative(cells[first_column], slot.rarity) ||
				!detail::parse_optional_nonnegative(cells[first_column + 1], slot.type) ||
				!detail::parse_optional_flag(cells[first_column + 3], slot.dupe_protection))
			{
				return std::nullopt;
			}

			slot.operation.assign(cells[first_column + 2]);
		}

		return drop;
	}


	std::optional<supply_drop> find_supply_drop(const catalog& source, std::string_view backend_id);
	std::shared_ptr<const catalog> get_snapshot();
}
