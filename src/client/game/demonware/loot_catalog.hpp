#pragma once

#include <charconv>
#include <span>

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
		bool menu_available{};

		bool operator==(const supply_drop&) const = default;
	};

	struct loot_item
	{
		std::uint32_t item_id{};
		int rarity{};
		int collection_id{};
		std::optional<int> operation{};
		std::optional<int> division{};
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
		std::uint32_t threshold{};
		std::uint32_t item_id{};
		std::uint32_t quantity{};
	};

	struct cod_point_bundle
	{
		std::string id{};
		std::string title{};
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

			const auto end = value.data() + value.size();
			const auto parsed = std::from_chars(value.data(), end, result);

			return parsed.ec == std::errc{} && parsed.ptr == end;
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

	inline std::optional<supply_drop> parse_supply_drop_row(const std::span<const std::string_view> cells)
	{
		constexpr std::size_t type_column = 1;
		constexpr std::size_t backend_id_column = 4;
		constexpr std::size_t item_id_column = 5;
		constexpr std::size_t zm_consumables_column = 11;
		constexpr std::size_t first_slot_column = 12;
		constexpr std::size_t slot_columns = 4;

		if (cells.size() <= 23 || cells[backend_id_column].empty())
		{
			return std::nullopt;
		}

		supply_drop drop{};
		if (!detail::parse_integer(cells[type_column], drop.type) ||
			!detail::parse_integer(cells[item_id_column], drop.item_id) || !drop.item_id ||
			!detail::parse_optional_flag(cells[zm_consumables_column], drop.contains_zm_consumables))
		{
			return std::nullopt;
		}

		drop.backend_id.assign(cells[backend_id_column]);
		// Visible/ShouldShowSplash distinguish player-facing crates from login
		// rewards and internal test rows; compatibility handles Legendary separately.
		drop.menu_available = !cells[2].empty() && !cells[7].empty() && !cells[9].empty() &&
			(cells[6] == "1" || cells[8] == "1");

		for (std::size_t index = 0; index < drop.slots.size(); ++index)
		{
			const auto column = first_slot_column + index * slot_columns;

			auto& slot = drop.slots[index];
			if (!detail::parse_optional_nonnegative(cells[column], slot.rarity) ||
				!detail::parse_optional_nonnegative(cells[column + 1], slot.type) ||
				!detail::parse_optional_flag(cells[column + 3], slot.dupe_protection))
			{
				return std::nullopt;
			}

			slot.operation.assign(cells[column + 2]);
		}

		return drop;
	}

	std::optional<supply_drop> find_supply_drop(const catalog& source, std::string_view backend_id);
	std::shared_ptr<const catalog> get_snapshot();
}
