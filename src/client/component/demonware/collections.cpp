#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "component/scheduler.hpp"
#include "component/console/console.hpp"

#include "game/demonware/marketplace/collection_catalog.hpp"
#include "game/demonware/loot/compatibility.hpp"
#include "game/game.hpp"

#include <charconv>
#include <unordered_map>

namespace demonware_collections
{
	namespace
	{
		using namespace demonware;

		constexpr int max_table_rows = 100000;
		constexpr int max_table_columns = 256;
		constexpr std::size_t max_cell_length = 256;
		constexpr std::uint32_t max_collection_items = 15;

		constexpr int collections_min_columns = 3;
		constexpr int collections_items_column = 3;

		constexpr int stats_key_column = 18;
		constexpr int stats_inventory_first_column = 35;
		constexpr int stats_inventory_last_column = 38;
		constexpr int stats_collection_column = 46;
		constexpr int stats_reward_column = 47;

		struct item_definition
		{
			std::uint32_t collection_id{};
			bool reward{};
			bool inventory{};
		};

		struct table_copy
		{
			std::vector<collection_catalog::collection> collections;
			std::unordered_map<std::uint32_t, item_definition> items;
			bool have_collections{};
			bool have_stats{};
			bool invalid{};
		};

		std::string_view cell(const game::StringTable* table, const int row, const int column)
		{
			const auto* text = table->values[row * table->columnCount + column].string;
			if (!text)
			{
				throw std::runtime_error("Missing collection table cell");
			}

			const auto length = strnlen_s(text, max_cell_length + 1);
			if (length > max_cell_length)
			{
				throw std::runtime_error("Oversized collection table cell");
			}

			return {text, length};
		}

		bool number(std::string_view text, std::uint32_t& value, const int base = 10)
		{
			if (base == 16 && (text.starts_with("0x") || text.starts_with("0X")))
			{
				text.remove_prefix(2);
			}

			if (text.empty())
			{
				return false;
			}

			const auto end = text.data() + text.size();
			const auto parsed = std::from_chars(text.data(), end, value, base);
			return parsed.ec == std::errc{} && parsed.ptr == end;
		}

		bool is_valid_table(const game::StringTable* table, const int min_columns_exclusive)
		{
			return table->values && table->rowCount > 0 && table->rowCount <= max_table_rows &&
				table->columnCount <= max_table_columns && table->columnCount > min_columns_exclusive;
		}

		std::optional<collection_catalog::collection> read_collection_row(const game::StringTable* table, const int row)
		{
			collection_catalog::collection entry;
			if (!number(cell(table, row, 0), entry.id) || !entry.id)
			{
				return std::nullopt;
			}

			std::uint32_t count{};
			if (!number(cell(table, row, 1), entry.reward, 16) || !entry.reward)
			{
				return std::nullopt;
			}

			if (!number(cell(table, row, 2), count) || !count || count > max_collection_items ||
				count > static_cast<std::uint32_t>(table->columnCount - collections_min_columns))
			{
				return std::nullopt;
			}

			for (std::uint32_t i = 0; i < count; ++i)
			{
				std::uint32_t id{};
				if (!number(cell(table, row, collections_items_column + static_cast<int>(i)), id, 16) || !id ||
					id == entry.reward || std::ranges::find(entry.items, id) != entry.items.end())
				{
					return std::nullopt;
				}

				entry.items.push_back(id);
			}

			return entry;
		}

		void read_item_row(const game::StringTable* table, const int row, table_copy& copy)
		{
			std::uint32_t id{};
			const auto key = cell(table, row, stats_key_column);
			if (!number(key, id, 16) || !loot_compatibility::matches_stock_item_guid_key(key, id))
			{
				return;
			}

			item_definition item;
			number(cell(table, row, stats_collection_column), item.collection_id);
			item.reward = cell(table, row, stats_reward_column) == "1";

			// Native 0xD0980 / 0x27A310: these items require inventory
			for (int column = stats_inventory_first_column; column <= stats_inventory_last_column; ++column)
			{
				item.inventory |= cell(table, row, column) == "1";
			}

			copy.items[id] = item;
		}

		void copy_table(const game::XAssetHeader header, void* context)
		{
			auto& copy = *static_cast<table_copy*>(context);
			const auto* table = header.stringTable;
			if (copy.invalid || !table || !table->name)
			{
				return;
			}

			const auto is_collections = !_stricmp(table->name, "mp/itemsCollections.csv");
			const auto is_stats = !_stricmp(table->name, "mp/statstable.csv");
			if (!is_collections && !is_stats)
			{
				return;
			}

			auto& have_table = is_collections ? copy.have_collections : copy.have_stats;
			const auto min_columns_exclusive = is_collections ? 2 : 47;

			// DB_EnumXAssets_FastFile (0xA0B00) holds its reader lock during this callback
			// Copy scalars only, never call back into the asset DB or unwind past the lock
			try
			{
				if (!is_valid_table(table, min_columns_exclusive) || have_table)
				{
					throw std::runtime_error("Invalid collection table");
				}

				have_table = true;

				for (int row = 0; row < table->rowCount; ++row)
				{
					if (is_collections)
					{
						auto entry = read_collection_row(table, row);
						if (entry)
						{
							copy.collections.push_back(std::move(*entry));
						}
					}
					else
					{
						read_item_row(table, row, copy);
					}
				}
			}
			catch (const std::exception&)
			{
				copy.invalid = true;
			}
		}

		bool is_collection_reward(const table_copy& copy, const collection_catalog::collection& entry)
		{
			const auto reward = copy.items.find(entry.reward);
			return reward != copy.items.end() && reward->second.inventory &&
				reward->second.reward && reward->second.collection_id == entry.id;
		}

		bool are_collection_members(const table_copy& copy, const collection_catalog::collection& entry)
		{
			return std::ranges::all_of(entry.items, [&](const auto id)
			{
				const auto found = copy.items.find(id);
				return found != copy.items.end() && found->second.inventory &&
					!found->second.reward && found->second.collection_id == entry.id;
			});
		}

		void refresh()
		{
			static std::shared_ptr<const loot_catalog::catalog> attempted;

			const auto source = loot_catalog::get_snapshot();
			if (!source || source == attempted)
			{
				return;
			}

			attempted = source;

			table_copy copy;
			game::DB_EnumXAssets_FastFile(game::ASSET_TYPE_STRINGTABLE, copy_table, &copy, false);
			if (copy.invalid || !copy.have_collections || !copy.have_stats ||
				source != loot_catalog::get_snapshot())
			{
				return;
			}

			collection_catalog::catalog result;
			result.source = source;

			std::unordered_set<std::uint32_t> ids;
			for (auto& entry : copy.collections)
			{
				if (!ids.insert(entry.id).second)
				{
					return;
				}

				if (!is_collection_reward(copy, entry) || !are_collection_members(copy, entry))
				{
					continue;
				}

				entry.rule = collection_catalog::rule_id(entry.id);
				result.purchasable_items.insert(entry.items.begin(), entry.items.end());
				result.collections.push_back(std::move(entry));
			}

#ifdef DEBUG
			console::debug("[DW] collection catalog: collections=%zu purchasableItems=%zu\n",
				result.collections.size(), result.purchasable_items.size());
#endif

			collection_catalog::publish(std::move(result));
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::is_dedicated())
			{
				scheduler::loop(refresh, scheduler::pipeline::main);
			}
		}
	};
}

REGISTER_COMPONENT(demonware_collections::component)
