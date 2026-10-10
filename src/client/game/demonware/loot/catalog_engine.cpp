#include <std_include.hpp>

#include "catalog_engine.hpp"
#include "compatibility.hpp"
#include "game/game.hpp"
#include "resource.hpp"

#include <utils/nt.hpp>
#include <utils/string.hpp>

#include <charconv>
#include <unordered_map>

namespace demonware::loot_catalog_engine
{
	using namespace loot_catalog;

	namespace
	{
		constexpr auto supply_drop_table_name = "mp/supplyDropTypes.csv";
		constexpr auto stats_table_name = "mp/statstable.csv";
		constexpr auto social_score_table_name = "mp/socialscoretable.csv";
		constexpr auto zombie_table_name = "mp/zombieConsumablesTable.csv";
		constexpr auto store_table_name = "mp/ingamestore/winStoreConfig.csv";

		constexpr std::size_t maximum_cell_length = 256;
		constexpr std::size_t maximum_display_name_length = 1024;
		constexpr std::size_t maximum_validated_bytes = 16 * 1024 * 1024;
		constexpr int maximum_table_rows = 100000;
		constexpr int maximum_table_columns = 256;

		constexpr int minimum_stats_columns = 57;

		constexpr int stats_group_column = 0;
		constexpr int stats_name_column = 1;
		constexpr int stats_reference_column = 2;
		constexpr int stats_image_column = 4;
		constexpr int stats_guid_column = 18;
		constexpr int stats_hidden_item_column = 24;
		constexpr int stats_rarity_column = 29;
		constexpr int stats_rarity_eligible_column = 33;
		constexpr int stats_loot_item_column = 35;
		constexpr int stats_challenge_column = 36;
		constexpr int stats_entitlement_column = 37;
		constexpr int stats_collection_reward_column = 47;
		constexpr int stats_division_column = 48;
		constexpr int stats_operation_column = 52;

		constexpr std::array stats_columns
		{
			stats_group_column,
			stats_reference_column,
			stats_name_column,
			stats_image_column,
			stats_guid_column,
			stats_rarity_column,
			stats_rarity_eligible_column,
			stats_loot_item_column,
			stats_challenge_column,
			stats_entitlement_column,
			stats_collection_reward_column,
			stats_division_column,
			stats_operation_column,
		};

		using stats_cells = std::array<std::string_view, stats_columns.size()>;

		constexpr std::array supply_drop_columns{1, 2, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23};

		// socialScoreTable only has localized reward labels, so the item bindings come from retail kind-5 descriptors
		constexpr std::array<std::uint32_t, 20> social_rank_items
		{
			0, 0x240026F, 1, 0, 0x1012200, 0x240026E, 0x200066, 1, 0, 0x1015200,
			1, 0x240026C, 2, 0, 0x1016102, 2, 0, 0x20005F, 0x1055302, 0x8000D1,
		};

		constexpr std::array<std::string_view, 21> permanent_groups
		{
			"emote",
			"grip",
			"playercard_icon",
			"playercard_title",
			"uniforms",
			"costume",
			"face_camo",
			"weapon_charm",
			"weapon_class_camo",
			"site_reticle",
			"weapon_camo",
			"universal_camo",
			"weapon_reticle",
			"weapon_assault",
			"weapon_smg",
			"weapon_heavy",
			"weapon_sniper",
			"weapon_shotgun",
			"weapon_pistol",
			"weapon_projectile",
			"weapon_other",
		};

		struct localize_entry
		{
			const char* value;
			const char* name;
		};

		static_assert(sizeof(localize_entry) == 16);
		static_assert(offsetof(localize_entry, value) == 0);
		static_assert(offsetof(localize_entry, name) == 8);

		using loot_catalog::detail::parse_integer;

		std::string copy_display_name(const char* key)
		{
			const auto* entry = static_cast<const localize_entry*>(
				game::DB_FindXAssetHeader(game::ASSET_TYPE_LOCALIZE, key, false).data);

			if (!entry || !entry->value)
			{
				return {};
			}

			const auto length = strnlen_s(entry->value, maximum_display_name_length + 1);
			if (length > maximum_display_name_length)
			{
				return {};
			}

			return {entry->value, length};
		}

		bool is_table_ready(const char* name)
		{
			return game::DB_XAssetExists(game::ASSET_TYPE_STRINGTABLE, name) &&
				!game::DB_IsXAssetDefault(game::ASSET_TYPE_STRINGTABLE, name);
		}

		const game::StringTable* find_table(const char* name)
		{
			const auto* table = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE, name, false).stringTable;

			if (!table || !table->name ||
				strnlen_s(table->name, maximum_cell_length + 1) > maximum_cell_length ||
				_stricmp(table->name, name) || !table->values ||
				table->rowCount < 0 || table->rowCount > maximum_table_rows ||
				table->columnCount < 0 || table->columnCount > maximum_table_columns)
			{
				return nullptr;
			}

			return table;
		}

		const game::StringTable* find_ready_table(const char* name)
		{
			return is_table_ready(name) ? find_table(name) : nullptr;
		}

		std::optional<std::string_view> get_cell(const game::StringTable* table, const int row, const int column)
		{
			if (!table || !table->values || row < 0 || row >= table->rowCount ||
				column < 0 || column >= table->columnCount)
			{
				return std::nullopt;
			}

			const auto* value = table->values[row * table->columnCount + column].string;
			if (!value)
			{
				return std::nullopt;
			}

			const auto* terminator = static_cast<const char*>(std::memchr(value, '\0', maximum_cell_length + 1));
			if (!terminator)
			{
				return std::nullopt;
			}

			return std::string_view{value, terminator};
		}

		std::optional<std::uint32_t> parse_stock_item_guid_key(const std::string_view value)
		{
			if (value.size() < 3 || value[0] != '0' || (value[1] != 'x' && value[1] != 'X'))
			{
				return std::nullopt;
			}

			std::uint32_t item_guid{};

			const auto end = value.data() + value.size();
			const auto parsed = std::from_chars(value.data() + 2, end, item_guid, 16);

			if (parsed.ec != std::errc{} || parsed.ptr != end ||
				!loot_compatibility::matches_stock_item_guid_key(value, item_guid))
			{
				return std::nullopt;
			}

			return item_guid;
		}

		template <int Column>
		std::string_view cell(const stats_cells& row)
		{
			constexpr auto index = static_cast<std::size_t>(std::ranges::find(stats_columns, Column) - stats_columns.begin());
			static_assert(index < stats_columns.size());

			return row[index];
		}

		std::optional<supply_drop> read_supply_drop(const game::StringTable* table, const int row)
		{
			std::array<std::string_view, 24> cells{};

			for (const auto column : supply_drop_columns)
			{
				const auto value = get_cell(table, row, column);
				if (!value)
				{
					return std::nullopt;
				}

				cells[static_cast<std::size_t>(column)] = *value;
			}

			return parse_supply_drop_row(cells);
		}

		std::vector<supply_drop> read_supply_drops()
		{
			std::vector<supply_drop> result{};

			const auto* table = find_table(supply_drop_table_name);
			if (!table || table->columnCount <= 23)
			{
				return result;
			}

			result.reserve(table->rowCount);

			for (auto row = 0; row < table->rowCount; ++row)
			{
				if (auto drop = read_supply_drop(table, row))
				{
					result.push_back(std::move(*drop));
				}
			}

			std::ranges::sort(result, [](const supply_drop& left, const supply_drop& right)
			{
				return std::tie(left.type, left.backend_id, left.item_id) < std::tie(right.type, right.backend_id, right.item_id);
			});

			const auto duplicates = std::ranges::unique(result);
			result.erase(duplicates.begin(), duplicates.end());

			return result;
		}

		bool has_confirmed_mp_drops(const catalog& source)
		{
			for (const auto* id : {"sd_mp", "sd_mp_rare"})
			{
				const auto drop = find_supply_drop(source, id);
				if (!drop || !loot_compatibility::is_confirmed_mp_supply_drop(*drop))
				{
					return false;
				}
			}

			return true;
		}

		std::vector<social_rank_reward> read_social_ranks()
		{
			const auto* table = find_ready_table(social_score_table_name);
			if (!table)
			{
				return {};
			}

			std::array<social_rank_reward, social_rank_items.size()> ranks{};

			for (auto row = 0; row < table->rowCount; ++row)
			{
				const auto key = get_cell(table, row, 0);
				const auto score = get_cell(table, row, 1);
				const auto count = get_cell(table, row, 6);

				std::size_t rank{};
				if (!key || !parse_integer(*key, rank) || rank >= ranks.size() || !score || !count)
				{
					continue;
				}

				auto& reward = ranks[rank];
				if (parse_integer(*score, reward.threshold) && parse_integer(*count, reward.quantity))
				{
					reward.item_id = social_rank_items[rank];
				}
			}

			const auto complete = std::ranges::all_of(ranks, [](const social_rank_reward& rank)
			{
				return rank.threshold && rank.quantity;
			});

			if (!complete)
			{
				return {};
			}

			return {ranks.begin(), ranks.end()};
		}

		std::unordered_map<std::string, std::uint32_t> read_zombie_table(const game::StringTable* table)
		{
			std::unordered_map<std::string, std::uint32_t> stock{};
			if (!table || table->columnCount < 4)
			{
				return stock;
			}

			for (auto row = 0; row < table->rowCount; ++row)
			{
				const auto reference = get_cell(table, row, 0);
				const auto image = get_cell(table, row, 1);
				const auto name = get_cell(table, row, 2);

				if (!reference || reference->empty() || !image || image->empty() || !name || name->empty())
				{
					continue;
				}

				stock[std::string{*reference}] = game::BG_GetItemGUIDFromReference(reference->data());
			}

			return stock;
		}

		// MP ships the consumable StatsTable records but not the Zombies CAC table, so its references are mirrored
		std::unordered_map<std::string, std::uint32_t> read_zombie_resource()
		{
			std::unordered_map<std::string, std::uint32_t> stock{};

			for (auto reference : utils::string::split(utils::nt::load_resource(DW_ZOMBIE_CONSUMABLE_REFERENCES), '\n'))
			{
				if (!reference.empty() && reference.back() == '\r')
				{
					reference.pop_back();
				}

				if (!reference.empty())
				{
					stock[reference] = game::BG_GetItemGUIDFromReference(reference.data());
				}
			}

			return stock;
		}

		std::unordered_map<std::string, std::uint32_t> read_zombie_stock()
		{
			if (is_table_ready(zombie_table_name))
			{
				return read_zombie_table(find_table(zombie_table_name));
			}

			return read_zombie_resource();
		}

		std::optional<std::unordered_map<std::uint32_t, bool>> read_hidden_items(const game::StringTable* table)
		{
			std::unordered_map<std::uint32_t, bool> hidden_items{};
			hidden_items.reserve(table->rowCount);

			for (auto row = 0; row < table->rowCount; ++row)
			{
				const auto guid = get_cell(table, row, stats_guid_column);
				const auto hidden = get_cell(table, row, stats_hidden_item_column);

				if (!guid || !hidden)
				{
					return std::nullopt;
				}

				// S2 searches backward, so the last matching row wins
				if (const auto item_guid = parse_stock_item_guid_key(*guid))
				{
					hidden_items[*item_guid] = loot_compatibility::stock_hidden_item_cell_is_true(*hidden);
				}
			}

			return hidden_items;
		}

		std::optional<stats_cells> read_stats_row(const game::StringTable* table, const int row, std::size_t& validated_bytes)
		{
			stats_cells cells{};

			for (std::size_t index = 0; index < stats_columns.size(); ++index)
			{
				const auto value = get_cell(table, row, stats_columns[index]);
				if (!value)
				{
					return std::nullopt;
				}

				validated_bytes += value->size();
				if (validated_bytes > maximum_validated_bytes)
				{
					return std::nullopt;
				}

				cells[index] = *value;
			}

			return cells;
		}

		bool is_localized(const std::string_view name)
		{
			return !name.empty() &&
				game::DB_XAssetExists(game::ASSET_TYPE_LOCALIZE, name.data()) &&
				!game::DB_IsXAssetDefault(game::ASSET_TYPE_LOCALIZE, name.data());
		}

		item_definition make_definition(const stats_cells& cells, const std::unordered_map<std::uint32_t, bool>& hidden_items,
			const std::unordered_map<std::string, std::uint32_t>& zombie_stock)
		{
			item_definition definition{};
			definition.loot_flag = cell<stats_loot_item_column>(cells) == "1";
			definition.rarity_eligible = cell<stats_rarity_eligible_column>(cells) == "1";
			definition.challenge = cell<stats_challenge_column>(cells) == "1";
			definition.entitlement = cell<stats_entitlement_column>(cells) == "1";
			definition.collection_reward = cell<stats_collection_reward_column>(cells) == "1";

			const auto name = cell<stats_name_column>(cells);
			definition.localized_name = is_localized(name);

			if (definition.localized_name)
			{
				definition.display_name = copy_display_name(name.data());
			}

			auto& item = definition.item;
			item.reference = cell<stats_reference_column>(cells);
			item.group = cell<stats_group_column>(cells);

			definition.reference_valid = !item.reference.empty();
			definition.presentation_available = !name.empty() && !cell<stats_image_column>(cells).empty();
			definition.rarity_valid = parse_integer(cell<stats_rarity_column>(cells), item.rarity);

			if (const auto operation = cell<stats_operation_column>(cells); !operation.empty())
			{
				int parsed{};
				definition.operation_valid = parse_integer(operation, parsed) && parsed >= 0;

				if (definition.operation_valid)
				{
					item.operation = parsed;
				}
			}

			int division{};
			if (parse_integer(cell<stats_division_column>(cells), division) && division >= 0)
			{
				item.division = division;
			}

			// Weapon grades share a reference, so each row keeps its own GUID instead of BG_GetItemGUIDFromReference
			item.item_id = parse_stock_item_guid_key(cell<stats_guid_column>(cells)).value_or(0);

			if (const auto hidden = hidden_items.find(item.item_id); hidden != hidden_items.end())
			{
				item.stock_hidden_item = hidden->second;
			}

			item.stock_internal_costume_component = loot_compatibility::stock_internal_costume_component(item.item_id);
			item.azm_consumable = loot_compatibility::stock_azm_consumable(item.item_id);

			if (const auto stock = zombie_stock.find(item.reference); stock != zombie_stock.end())
			{
				definition.zombie_consumable_available = item.azm_consumable && stock->second == item.item_id;
			}

			return definition;
		}

		std::vector<std::uint32_t> read_permanent_customization(const std::vector<item_definition>& items)
		{
			std::unordered_map<std::uint32_t, bool> customization{};

			for (const auto& row : items)
			{
				if (row.item.item_id)
				{
					customization[row.item.item_id] = row.reference_valid &&
						std::ranges::find(permanent_groups, row.item.group) != permanent_groups.end();
				}
			}

			std::vector<std::uint32_t> result{};
			for (const auto& [id, permanent] : customization)
			{
				if (permanent)
				{
					result.push_back(id);
				}
			}

			std::ranges::sort(result);
			return result;
		}

		std::optional<cod_point_bundle> read_cod_point_bundle(const game::StringTable* table, const int row)
		{
			if (get_cell(table, row, 0) != "product" || get_cell(table, row, 4) != "codpoints")
			{
				return std::nullopt;
			}

			const auto id = get_cell(table, row, 1);
			const auto image = get_cell(table, row, 3);
			const auto title = get_cell(table, row, 13);

			if (!id || !title || title->empty() || !image || image->empty())
			{
				return std::nullopt;
			}

			const auto suffix = id->find("codpoints");
			if (suffix == std::string_view::npos)
			{
				return std::nullopt;
			}

			const auto variant = id->substr(suffix);
			if (variant != "codpoints" && variant != "codpointsB")
			{
				return std::nullopt;
			}

			std::uint32_t amount{};
			if (!parse_integer(id->substr(0, suffix), amount) || !amount || amount > INT32_MAX)
			{
				return std::nullopt;
			}

			return cod_point_bundle{std::string{*id}, std::string{*title}, amount, std::string{*image}};
		}

		std::vector<cod_point_bundle> read_cod_point_bundles()
		{
			std::vector<cod_point_bundle> result{};

			const auto* table = find_ready_table(store_table_name);
			if (!table)
			{
				return result;
			}

			for (auto row = 0; row < table->rowCount; ++row)
			{
				if (auto bundle = read_cod_point_bundle(table, row))
				{
					result.push_back(std::move(*bundle));
				}
			}

			return result;
		}
	}

	std::optional<catalog> build_catalog(const std::uint64_t generation, const std::uint32_t producer_thread_id)
	{
		if (!is_table_ready(stats_table_name) || !is_table_ready(supply_drop_table_name))
		{
			return std::nullopt;
		}

		const auto* table = find_table(stats_table_name);
		if (!table || table->rowCount <= 0 || table->columnCount < minimum_stats_columns)
		{
			return std::nullopt;
		}

		catalog result{};
		result.generation = generation;
		result.producer_thread_id = producer_thread_id;
		result.supply_drops = read_supply_drops();

		if (!has_confirmed_mp_drops(result))
		{
			return std::nullopt;
		}

		result.social_ranks = read_social_ranks();

		auto& observed = result.source_table;
		observed.table_asset_available = true;
		observed.table_values_available = true;
		observed.table_dimensions_valid = true;
		observed.required_columns_available = true;
		observed.table_row_count = table->rowCount;
		observed.table_column_count = table->columnCount;

		const auto zombie_stock = read_zombie_stock();

		const auto hidden_items = read_hidden_items(table);
		if (!hidden_items)
		{
			return std::nullopt;
		}

		result.items.reserve(table->rowCount);

		std::size_t validated_bytes{};
		for (auto row = 0; row < table->rowCount; ++row)
		{
			const auto cells = read_stats_row(table, row, validated_bytes);
			if (!cells)
			{
				return std::nullopt;
			}

			result.items.push_back(make_definition(*cells, *hidden_items, zombie_stock));
		}

		result.permanent_customization = read_permanent_customization(result.items);
		result.cod_point_bundles = read_cod_point_bundles();

		return result;
	}
}
