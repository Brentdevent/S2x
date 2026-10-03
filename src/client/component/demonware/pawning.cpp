#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "component/scheduler.hpp"
#include "component/console/console.hpp"

#include "game/game.hpp"
#include "game/demonware/pawn_catalog.hpp"
#include "game/demonware/collection_catalog.hpp"
#include "game/demonware/loot_compatibility.hpp"

#include <charconv>
#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace demonware_pawning
{
	namespace
	{
		constexpr int max_table_rows = 100000;
		constexpr int max_table_columns = 256;
		constexpr int min_table_columns = 39;
		constexpr std::size_t max_cell_length = 256;

		constexpr int key_column = 18;
		constexpr int hidden_column = 24;
		constexpr int inventory_first_column = 35;
		constexpr int inventory_last_column = 38;

		constexpr std::uint32_t pawn_currency = 6;

		struct table_copy
		{
			std::unordered_map<std::uint32_t, bool> items;
			bool found{};
			bool invalid{};
		};

		std::string_view cell(const game::StringTable* table, const int row, const int column)
		{
			const auto* text = table->values[row * table->columnCount + column].string;
			if (!text || strnlen_s(text, max_cell_length + 1) > max_cell_length)
			{
				throw std::runtime_error("Invalid pawn cell");
			}

			return text;
		}

		bool is_valid_table(const game::StringTable* table)
		{
			return table->values && table->rowCount > 0 && table->rowCount <= max_table_rows &&
				table->columnCount >= min_table_columns && table->columnCount <= max_table_columns;
		}

		bool parse_item_id(std::string_view key, std::uint32_t& id)
		{
			if (key.starts_with("0x") || key.starts_with("0X"))
			{
				key.remove_prefix(2);
			}

			const auto end = key.data() + key.size();
			const auto parsed = std::from_chars(key.data(), end, id, 16);
			return parsed.ec == std::errc{} && parsed.ptr == end;
		}

		void read_item_row(const game::StringTable* table, const int row, table_copy& copy)
		{
			const auto key = cell(table, row, key_column);

			std::uint32_t id{};
			if (!parse_item_id(key, id) || !demonware::loot_compatibility::matches_stock_item_guid_key(key, id))
			{
				return;
			}

			// 0x276330 excludes hidden items, 0xD0980 treats items without an inventory flag as owned
			bool inventory{};
			for (int column = inventory_first_column; column <= inventory_last_column; ++column)
			{
				inventory |= cell(table, row, column) == "1";
			}

			copy.items[id] = inventory && atoi(std::string{cell(table, row, hidden_column)}.c_str()) <= 0;
		}

		void copy_items(const game::XAssetHeader header, void* context)
		{
			auto& copy = *static_cast<table_copy*>(context);
			const auto* table = header.stringTable;
			if (!table || !table->name || _stricmp(table->name, "mp/statstable.csv"))
			{
				return;
			}

			try
			{
				if (copy.found || !is_valid_table(table))
				{
					throw std::runtime_error("Invalid pawn table");
				}

				copy.found = true;

				for (int row = 0; row < table->rowCount; ++row)
				{
					read_item_row(table, row, copy);
				}
			}
			catch (const std::exception&)
			{
				copy.invalid = true;
			}
		}

		bool is_uniform_item(const std::uint32_t id)
		{
			return ((id >> 24) & 7) == 6 && ((id >> 20) & 15) == 0;
		}

		void refresh()
		{
			using namespace demonware;

			static std::shared_ptr<const loot_catalog::catalog> attempted;

			const auto source = loot_catalog::get_snapshot();
			if (!source || source == attempted)
			{
				return;
			}

			attempted = source;

			table_copy copy;
			game::DB_EnumXAssets_FastFile(game::ASSET_TYPE_STRINGTABLE, copy_items, &copy, false);
			if (!copy.found || copy.invalid || source != loot_catalog::get_snapshot())
			{
				return;
			}

			pawn_catalog::catalog result;
			result.source = source;

			for (const auto& [id, eligible] : copy.items)
			{
				if (!eligible)
				{
					continue;
				}

				// Inventory_GetItemPawnValue must run on main, after the DB enumeration lock is released
				const auto value = utils::hook::invoke<std::uint64_t>(0x274CF0_g, id);
				const auto currency = static_cast<std::uint32_t>(value);
				const auto amount = static_cast<std::uint32_t>(value >> 32);
				if (currency != pawn_currency || !amount || amount > INT32_MAX)
				{
					continue;
				}

				pawn_catalog::item item{amount, static_cast<std::uint8_t>(currency)};

				// Routing matches the native duplicate pump 0x276C20
				if (is_uniform_item(id))
				{
					item.rule = collection_catalog::rule_id(utils::string::va("Pawnable_Uniform_%X", id));
				}

				result.items.emplace(id, std::move(item));
			}

			if (source != loot_catalog::get_snapshot())
			{
				return;
			}

#ifdef DEBUG
			console::debug("[DW] pawn catalog: items=%zu\n", result.items.size());
#endif

			pawn_catalog::publish(std::move(result));
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

REGISTER_COMPONENT(demonware_pawning::component)
