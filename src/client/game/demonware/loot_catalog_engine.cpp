#include <std_include.hpp>

#include "loot_catalog_engine.hpp"
#include "loot_compatibility.hpp"
#include "game/game.hpp"
#include "resource.hpp"
#include <utils/nt.hpp>
#include <utils/string.hpp>

#include <algorithm>
#include <charconv>
#include <tuple>
#include <unordered_map>

namespace demonware::loot_catalog_engine
{
	using namespace loot_catalog;
	namespace
	{
		constexpr auto supply_drop_table_name = "mp/supplyDropTypes.csv";
		constexpr auto stats_table_name = "mp/statstable.csv";
		constexpr std::size_t maximum_cell_length = 256;
		constexpr int maximum_table_rows = 100000;
		constexpr int maximum_table_columns = 256;
		constexpr int stats_group_column = 0;
		constexpr int stats_reference_column = 2;
		constexpr int stats_name_column = 1;
		constexpr int stats_image_column = 4;
		constexpr int stats_guid_column = 18;
		constexpr int stats_ignore_column = 20;
		constexpr int stats_hidden_item_column = 24;
		constexpr int stats_rarity_column = 29;
		constexpr int stats_rarity_eligible_column = 33;
		constexpr int stats_loot_item_column = 35;
		constexpr int stats_challenge_column = 36;
		constexpr int stats_entitlement_column = 37;
		constexpr int stats_collection_column = 46;
		constexpr int stats_collection_reward_column = 47;
		constexpr int stats_operation_column = 52;
		constexpr int stats_production_level_column = 56;
		constexpr std::array stats_columns{
			stats_group_column, stats_reference_column, stats_name_column, stats_image_column,
			stats_guid_column, stats_ignore_column,
			stats_hidden_item_column, stats_rarity_column, stats_rarity_eligible_column,
			stats_loot_item_column, stats_challenge_column, stats_entitlement_column,
			stats_collection_column, stats_collection_reward_column, stats_operation_column, stats_production_level_column
		};
		using stats_cells = std::array<std::string_view, stats_columns.size()>;

		// Native LOCALIZE assets store value at +0 and key at +8 (x64).
		struct localize_entry
		{
			const char* value;
			const char* name;
		};
		static_assert(sizeof(localize_entry) == 16);
		static_assert(offsetof(localize_entry, value) == 0);
		static_assert(offsetof(localize_entry, name) == 8);

		std::string copy_display_name(const char* key)
		{
			const auto* entry = static_cast<const localize_entry*>(
				game::DB_FindXAssetHeader(game::ASSET_TYPE_LOCALIZE, key, false).data);
			if (!entry || !entry->value)
			{
				return {};
			}

			constexpr std::size_t maximum_name_length = 1024;
			const auto length = strnlen_s(entry->value, maximum_name_length + 1);
			return length <= maximum_name_length ? std::string(entry->value, length) : std::string{};
		}

		const game::StringTable* find_table(const char* name)
		{
			const auto* table = game::DB_FindXAssetHeader(
				game::ASSET_TYPE_STRINGTABLE, name, false).stringTable;
			if (!table || !table->name ||
				strnlen_s(table->name, maximum_cell_length + 1) > maximum_cell_length ||
				_stricmp(table->name, name) || !table->values || table->rowCount < 0 ||
				table->rowCount > maximum_table_rows || table->columnCount < 0 ||
				table->columnCount > maximum_table_columns)
			{
				return nullptr;
			}

			return table;
		}

		std::optional<std::string_view> get_cell(const game::StringTable* table,
			const int row, const int column)
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

			const auto* terminator = static_cast<const char*>(
				std::memchr(value, '\0', maximum_cell_length + 1));
			if (!terminator)
			{
				return std::nullopt;
			}

			return std::string_view{value, static_cast<std::size_t>(terminator - value)};
		}

		using loot_catalog::detail::parse_integer;

		std::optional<std::uint32_t> parse_stock_item_guid_key(const std::string_view value)
		{
			if (value.size() < 3 || value[0] != '0' || (value[1] != 'x' && value[1] != 'X'))
			{
				return std::nullopt;
			}
			std::uint32_t item_guid{};
			const auto parsed = std::from_chars(value.data() + 2, value.data() + value.size(), item_guid, 16);
			return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() &&
				loot_compatibility::matches_stock_item_guid_key(value, item_guid)
				? std::optional{item_guid} : std::nullopt;
		}

		template <int Column>
		std::string_view cell(const stats_cells& row)
		{
			constexpr auto index = []
			{
				for (std::size_t index = 0; index < stats_columns.size(); ++index)
				{
					if (stats_columns[index] == Column)
					{
						return index;
					}
				}
				return stats_columns.size();
			}();
			static_assert(index < stats_columns.size());
			return row[index];
		}

		bool named_table_is_ready(const char* name)
		{
			return game::DB_XAssetExists(game::ASSET_TYPE_STRINGTABLE, name) &&
				!game::DB_IsXAssetDefault(game::ASSET_TYPE_STRINGTABLE, name);
		}
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
			std::array<std::string_view, 24> cells{};
			constexpr std::array required_columns{
				1, 4, 5, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23
			};
			bool row_available = true;
			for (const auto column : required_columns)
			{
				const auto cell = get_cell(table, row, column);
				if (!cell)
				{
					row_available = false;
					break;
				}

				cells[static_cast<std::size_t>(column)] = *cell;
			}
			if (!row_available)
			{
				continue;
			}

			auto drop = parse_supply_drop_row(cells);
			if (!drop)
			{
				continue;
			}

			result.emplace_back(std::move(*drop));
		}

		std::ranges::sort(result, [](const supply_drop& left, const supply_drop& right)
		{
			return std::tie(left.type, left.backend_id, left.item_id) <
				std::tie(right.type, right.backend_id, right.item_id);
		});
		result.erase(std::unique(result.begin(), result.end(),
			[](const supply_drop& left, const supply_drop& right)
			{
				return left == right;
			}), result.end());
		return result;
	}

	std::optional<catalog> build_catalog(const std::uint64_t generation,
		const std::uint32_t producer_thread_id)
	{

		catalog result{};
		result.generation = generation;
		result.producer_thread_id = producer_thread_id;
		const game::StringTable* table{};
		{

			if (!named_table_is_ready(stats_table_name) || !named_table_is_ready(supply_drop_table_name))
			{
				return std::nullopt;
			}
			table = find_table(stats_table_name);
			if (!table || table->rowCount <= 0 || table->columnCount <= stats_production_level_column)
			{
				return std::nullopt;
			}
			result.supply_drops = read_supply_drops();
			for (const auto id : {"sd_mp", "sd_mp_rare"})
			{
				const auto drop = find_supply_drop(result, id);
				if (!drop || !loot_compatibility::is_confirmed_mp_supply_drop(*drop))
				{
					return std::nullopt;
				}
			}
		}
		// socialScoreTable describes the rank thresholds/counts but uses localized
		// reward labels rather than inventory IDs. Early bindings are captured in
		// the retail kind-5 descriptors; later bindings follow the displayed stock
		// rewards/StatsTable variants as local policy (not a recovered backend pool).
		constexpr std::array<std::uint32_t, 20> social_items{
			0, 0x240026F, 1, 0, 0x1012200, 0x240026E, 0x200066, 1, 0, 0x1015200,
			1, 0x240026C, 2, 0, 0x1016102, 2, 0, 0x20005F, 0x1055302, 0x8000D1};
		if (named_table_is_ready("mp/socialscoretable.csv"))
		{
			const auto* social = find_table("mp/socialscoretable.csv");
			std::array<social_rank_reward, 20> ranks{};
			for (int row = 0; social && row < social->rowCount; ++row)
			{
				int rank{};
				const auto key = get_cell(social, row, 0);
				if (!key || !parse_integer(*key, rank) || rank < 0 || rank >= ranks.size())
				{
					continue;
				}
				const auto score = get_cell(social, row, 1), count = get_cell(social, row, 6);
				if (!score || !count || !parse_integer(*score, ranks[rank].threshold) ||
					!parse_integer(*count, ranks[rank].quantity))
				{
					continue;
				}
				ranks[rank].item_id = social_items[rank];
			}
			if (std::ranges::all_of(ranks, [](const auto& rank) { return rank.threshold && rank.quantity; }))
			{
				result.social_ranks.assign(ranks.begin(), ranks.end());
			}
		}
		const auto row_count = static_cast<std::size_t>(table->rowCount);

		auto& observed = result.source_table;
		observed.table_asset_available = true;
		observed.table_values_available = true;
		observed.table_dimensions_valid = true;
		observed.required_columns_available = true;
		observed.table_row_count = table->rowCount;
		observed.table_column_count = table->columnCount;

		std::unordered_map<std::string, std::uint32_t> zombie_stock;
		constexpr auto zombie_table_name = "mp/zombieConsumablesTable.csv";
		if (named_table_is_ready(zombie_table_name))
		{
			if (const auto* zombies = find_table(zombie_table_name); zombies && zombies->columnCount >= 4)
			{
				for (int row = 0; row < zombies->rowCount; ++row)
				{
					const auto reference = get_cell(zombies, row, 0);
					const auto image = get_cell(zombies, row, 1);
					const auto name = get_cell(zombies, row, 2);
					if (!reference || reference->empty() || !image || image->empty() || !name || name->empty())
					{
						continue;
					}
					// ZMCacUtils builds stock using this exact reference lookup. Each
					// rarity variant owns a separate row, including non-bitfield GUIDs.
					zombie_stock[std::string{*reference}] = game::BG_GetItemGUIDFromReference(reference->data());
				}
			}
		}
		else
		{
			// MP/HQ ships the consumable StatsTable records, but not the Zombies
			// CAC table. Mirror only that native table's playable references in a
			// runtime resource; resolve GUIDs/rarity/presentation from live data.
			// Prefer the actual table whenever loaded. This is not a retail pool.
			for (auto reference : utils::string::split(utils::nt::load_resource(DW_ZOMBIE_CONSUMABLE_REFERENCES), '\n'))
			{
				if (!reference.empty() && reference.back() == '\r')
				{
					reference.pop_back();
				}
				if (!reference.empty())
				{
					zombie_stock[reference] = game::BG_GetItemGUIDFromReference(reference.c_str());
				}
			}
		}

		std::unordered_map<std::uint32_t, bool> hidden_items{};
		hidden_items.reserve(row_count);
		for (int row = 0; row < table->rowCount; ++row)
		{
			const auto guid_cell = get_cell(table, row, stats_guid_column);
			const auto hidden_cell = get_cell(table, row, stats_hidden_item_column);
			if (!guid_cell || !hidden_cell)
			{
				return std::nullopt;
			}
			if (const auto guid = parse_stock_item_guid_key(*guid_cell))
			{
				// S2 searches backward: replacing forward rows preserves final match.
				hidden_items[*guid] = loot_compatibility::stock_hidden_item_cell_is_true(
					*hidden_cell);
			}
		}
		result.items.reserve(row_count);
		std::size_t validated_bytes{};
		constexpr std::size_t maximum_validated_bytes = 16 * 1024 * 1024;

		for (int row = 0; row < table->rowCount; ++row)
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
			// Views live only for this row. Copy the retained metadata fields
			// directly into their final owner; flags and numbers need no allocation.
			item_definition definition{};
			auto& item = definition.item;
			definition.loot_flag = cell<stats_loot_item_column>(cells) == "1";
			definition.rarity_eligible = cell<stats_rarity_eligible_column>(cells) == "1";
			definition.challenge = cell<stats_challenge_column>(cells) == "1";
			definition.entitlement = cell<stats_entitlement_column>(cells) == "1";
			definition.collection_reward = cell<stats_collection_reward_column>(cells) == "1";
			const auto name = cell<stats_name_column>(cells);
			definition.localized_name = !name.empty() &&
				game::DB_XAssetExists(game::ASSET_TYPE_LOCALIZE, name.data()) &&
				!game::DB_IsXAssetDefault(game::ASSET_TYPE_LOCALIZE, name.data());
			if (definition.localized_name)
			{
				// Copy while assets are pinned by the catalog producer. Console commands
				// only read owned snapshot strings, never localization/asset pointers.
				definition.display_name = copy_display_name(name.data());
			}
			item.reference = cell<stats_reference_column>(cells);
			item.group = cell<stats_group_column>(cells);
			item.ignore = cell<stats_ignore_column>(cells);
			item.hidden_item = cell<stats_hidden_item_column>(cells);
			item.production_level = cell<stats_production_level_column>(cells);
			definition.reference_valid = !item.reference.empty();
			definition.presentation_available = !cell<stats_name_column>(cells).empty() &&
				!cell<stats_image_column>(cells).empty();
			definition.rarity_valid = parse_integer(cell<stats_rarity_column>(cells), item.rarity);
			if (const auto collection = cell<stats_collection_column>(cells); !collection.empty())
			{
				definition.collection_valid = parse_integer(collection, item.collection_id);
			}
			if (const auto operation = cell<stats_operation_column>(cells); !operation.empty())
			{
				int parsed{};
				definition.operation_valid = parse_integer(operation, parsed) && parsed >= 0;
				if (definition.operation_valid)
				{
					item.operation = parsed;
				}
			}
			// Keep each native GUID with its own row metadata. Weapon grades share a
			// reference: BG_GetItemGUIDFromReference (0x6524C0) resolves only the last
			// matching row and would pair heroic rarity with a different condition.
			item.item_id = parse_stock_item_guid_key(cell<stats_guid_column>(cells)).value_or(0);
			if (const auto hidden = hidden_items.find(item.item_id); hidden != hidden_items.end())
			{
				item.stock_hidden_item = hidden->second;
			}
			item.stock_internal_costume_component =
				loot_compatibility::stock_internal_costume_component(item.item_id);
			item.azm_consumable = loot_compatibility::stock_azm_consumable(item.item_id);
			if (const auto stock = zombie_stock.find(item.reference); stock != zombie_stock.end())
			{
				definition.zombie_consumable_available = item.azm_consumable && stock->second == item.item_id;
			}
			result.items.push_back(std::move(definition));
		}

		// Accessibility policy, not crate eligibility. These native groups describe
		// permanent customization; boosters, contracts, consumables, flags and
		// other transactional records keep their real inventory quantities.
		constexpr std::array<std::string_view, 21> permanent_groups{
			"emote", "grip", "playercard_icon", "playercard_title", "uniforms", "costume",
			"face_camo", "weapon_charm", "weapon_class_camo", "site_reticle",
			"weapon_camo", "universal_camo", "weapon_reticle",
			"weapon_assault", "weapon_smg", "weapon_heavy", "weapon_sniper",
			"weapon_shotgun", "weapon_pistol", "weapon_projectile", "weapon_other"
		};
		std::unordered_map<std::uint32_t, bool> customization;
		for (const auto& row : result.items)
		{
			// As with native StatsTable lookups, the last alias wins.
			if (row.item.item_id)
			{
				customization[row.item.item_id] = row.reference_valid &&
					std::ranges::find(permanent_groups, row.item.group) != permanent_groups.end();
			}
		}
		for (const auto& [id, permanent] : customization)
		{
			if (permanent)
			{
				result.permanent_customization.push_back(id);
			}
		}
		std::ranges::sort(result.permanent_customization);

		// The Windows storefront normally supplements these products with Steam
		// metadata. Local top-ups use only its native codpoints products and their
		// numeric bundle denomination; no platform price/payment is emulated.
		constexpr auto store_table_name = "mp/ingamestore/winStoreConfig.csv";
		if (named_table_is_ready(store_table_name))
		{
			const auto* store = find_table(store_table_name);
			for (int row = 0; store && row < store->rowCount; ++row)
			{
				if (get_cell(store, row, 0) != "product" || get_cell(store, row, 4) != "codpoints")
				{
					continue;
				}
				const auto id = get_cell(store, row, 1), title = get_cell(store, row, 13);
				const auto image = get_cell(store, row, 3);
				if (!id || !title || title->empty() || !image || image->empty())
				{
					continue;
				}
				const auto suffix = id->find("codpoints");
				if (suffix == std::string_view::npos || (id->substr(suffix) != "codpoints" && id->substr(suffix) != "codpointsB"))
				{
					continue;
				}
				std::uint32_t amount{};
				if (parse_integer(id->substr(0, suffix), amount) && amount && amount <= INT32_MAX)
				{
					result.cod_point_bundles.push_back({std::string{*id}, std::string{*title}, amount, std::string{*image}});
				}
			}
		}
		return result;
	}
}
