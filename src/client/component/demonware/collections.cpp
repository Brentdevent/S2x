#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "component/scheduler.hpp"
#include "component/console/console.hpp"

#include "game/demonware/collection_catalog.hpp"
#include "game/demonware/loot_compatibility.hpp"
#include "game/game.hpp"

#include <charconv>
#include <unordered_map>

namespace demonware_collections
{
	namespace
	{
		using namespace demonware;
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
			bool have_collections{}, have_stats{}, invalid{};
		};

		std::string_view cell(const game::StringTable* table, const int row, const int column)
		{
			const auto* text = table->values[row * table->columnCount + column].string;
			if (!text)
			{
				throw std::runtime_error("Missing collection table cell");
			}
			const auto length = strnlen_s(text, 257);
			if (length > 256)
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
			const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, base);
			return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
		}

		void copy_table(const game::XAssetHeader header, void* context)
		{
			auto& copy = *static_cast<table_copy*>(context);
			const auto* table = header.stringTable;
			if (copy.invalid || !table || !table->name)
			{
				return;
			}
			const auto collections = !_stricmp(table->name, "mp/itemsCollections.csv");
			const auto stats = !_stricmp(table->name, "mp/statstable.csv");
			if (!collections && !stats)
			{
				return;
			}
			// DB_EnumXAssets_FastFile (0xA0B00) holds its reader lock throughout
			// this callback. Copy scalars only; never call back into the asset DB.
			try
			{
				if (!table->values || table->rowCount <= 0 || table->rowCount > 100000 ||
					table->columnCount > 256 || table->columnCount <= (collections ? 2 : 47) ||
					(collections ? copy.have_collections : copy.have_stats))
				{
					throw std::runtime_error("Invalid collection table");
				}
				(collections ? copy.have_collections : copy.have_stats) = true;
				for (int row = 0; row < table->rowCount; ++row)
				{
					if (collections)
					{
						collection_catalog::collection entry;
						std::uint32_t count{};
						if (!number(cell(table, row, 0), entry.id) || !entry.id)
						{
							continue;
						}
						if (!number(cell(table, row, 1), entry.reward, 16) || !entry.reward ||
							!number(cell(table, row, 2), count) || !count || count > 15 ||
							count > static_cast<std::uint32_t>(table->columnCount - 3))
						{
							continue;
						}
						for (std::uint32_t i = 0; i < count; ++i)
						{
							std::uint32_t id{};
							if (!number(cell(table, row, 3 + static_cast<int>(i)), id, 16) || !id ||
								id == entry.reward || std::ranges::find(entry.items, id) != entry.items.end())
							{
								break;
							}
							entry.items.push_back(id);
						}
						if (entry.items.size() == count)
						{
							copy.collections.push_back(std::move(entry));
						}
					}
					else
					{
						std::uint32_t id{};
						const auto key = cell(table, row, 18);
						if (!number(key, id, 16) || !loot_compatibility::matches_stock_item_guid_key(key, id))
						{
							continue;
						}
						item_definition item;
						number(cell(table, row, 46), item.collection_id);
						item.reward = cell(table, row, 47) == "1";
						// Native 0xD0980 / 0x27A310: these items require inventory.
						for (int column = 35; column <= 38; ++column)
						{
							item.inventory |= cell(table, row, column) == "1";
						}
						copy.items[id] = item; // Match the stock reverse GUID lookup.
					}
				}
			}
			catch (const std::exception&)
			{
				copy.invalid = true;
			} // Do not unwind past the engine lock.
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
				const auto reward = copy.items.find(entry.reward);
				if (reward == copy.items.end() || !reward->second.inventory ||
					!reward->second.reward || reward->second.collection_id != entry.id)
				{
					continue;
				}
				const auto valid = std::ranges::all_of(entry.items, [&](const auto id)
				{
					const auto found = copy.items.find(id);
					return found != copy.items.end() && found->second.inventory &&
						!found->second.reward && found->second.collection_id == entry.id;
				});
				if (!valid)
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
