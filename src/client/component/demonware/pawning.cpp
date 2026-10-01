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
		struct table_copy
		{
			std::unordered_map<std::uint32_t, bool> items;
			bool found{}, invalid{};
		};

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
				if (copy.found || !table->values || table->rowCount <= 0 || table->rowCount > 100000 ||
					table->columnCount < 39 || table->columnCount > 256)
				{
					throw std::runtime_error("Invalid pawn table");
				}
				copy.found = true;
				for (int row = 0; row < table->rowCount; ++row)
				{
					const auto cell = [&](const int column) -> std::string_view
					{
						const auto* text = table->values[row * table->columnCount + column].string;
						if (!text || strnlen_s(text, 257) > 256)
						{
							throw std::runtime_error("Invalid pawn cell");
						}
						return text;
					};
					auto key = cell(18);
					if (key.starts_with("0x") || key.starts_with("0X"))
					{
						key.remove_prefix(2);
					}
					std::uint32_t id{};
					const auto parsed = std::from_chars(key.data(), key.data() + key.size(), id, 16);
					if (parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size() ||
						!demonware::loot_compatibility::matches_stock_item_guid_key(cell(18), id))
					{
						continue;
					}
					// 0x276330 excludes hidden items; 0xD0980 treats items without
					// any of these inventory flags as inherently owned.
					bool inventory{};
					for (int column = 35; column <= 38; ++column)
					{
						inventory |= cell(column) == "1";
					}
					copy.items[id] = inventory && atoi(std::string{cell(24)}.c_str()) <= 0;
				}
			}
			catch (const std::exception&)
			{
				copy.invalid = true;
			}
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
				// Native Inventory_GetItemPawnValue: checks pawnable, decodes the
				// GUID subtype, then indexes mp/pawnValues.csv using stock rarity.
				// Call on the main thread, after releasing the DB enumeration lock.
				const auto value = utils::hook::invoke<std::uint64_t>(0x274CF0_g, id);
				const auto currency = static_cast<std::uint32_t>(value);
				const auto amount = static_cast<std::uint32_t>(value >> 32);
				if (currency != 6 || !amount || amount > INT32_MAX)
				{
					continue;
				}
				pawn_catalog::item item{amount, static_cast<std::uint8_t>(currency)};
				// Exact routing in the native duplicate pump (0x276C20).
				if (((id >> 24) & 7) == 6 && ((id >> 20) & 15) == 0)
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
