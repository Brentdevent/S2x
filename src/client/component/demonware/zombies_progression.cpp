#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "zombies_progression.hpp"
#include "component/achievement_sync.hpp"
#include "economy.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "game/string_table.hpp"
#include "game/demonware/achievement_store.hpp"
#include "game/demonware/loot_catalog.hpp"
#include "game/demonware/runtime_context.hpp"

namespace zombies_progression
{
	namespace
	{
		using namespace demonware;
		using namespace game::string_table;

		struct binding { int id, event; const char* event_name; };
		constexpr std::array bindings{
			binding{344, 16, "zombies"},
			binding{1112, 41, "zombies_map_won"},
			binding{1114, 42, "zombies_dlc3_sv_unlock"},
			binding{759, 43, "zombies_dlc3_ee_unlock"},
			binding{758, 43, "zombies_dlc3_ee_unlock"},
			binding{760, 43, "zombies_dlc3_ee_unlock"},
			binding{761, 44, "zombies_dlc3_skull_unlock"}};
		// _achievement_engine_z_utils::add_current_map / ZMUtils.Value_Zm_Map_*.
		// These are map selector values, NOT chapter indices. Stock LUI checks
		// hasbit(AE1112.progress, bit(mapValue + 1)), i.e. 1 << mapValue.
		constexpr std::array maps{"mp_zombie_windmill", "mp_zombie_dnk", "mp_zombie_dig_02"};
		constexpr unsigned first_map = 5;

		struct definitions
		{
			std::array<achievement_record, bindings.size()> records;
			std::uint32_t house_item{};
		};
		std::atomic<std::shared_ptr<const definitions>> current;

		bool load_definitions()
		{
			const auto catalog = loot_catalog::get_snapshot();
			if (!catalog)
			{
				return false;
			}
			const auto* challenges = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,
				"dw/dwGameChallenges.csv", false).stringTable;
			const auto* events = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,
				"dw/dwGameEvents.csv", false).stringTable;
			const auto* chapters = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,
				"mp/zombieDLC3MapInfoTable.csv", false).stringTable;
			auto result = std::make_shared<definitions>();
			for (std::size_t index = 0; index < bindings.size(); ++index)
			{
				const auto& binding = bindings[index];
				const auto row = find_row(challenges, 0, std::to_string(binding.id));
				const auto event_row = find_row(events, 0, std::to_string(binding.event));
				const auto* name = get_cell(challenges, row, 1);
				const auto* predicate = get_cell(challenges, row, 4);
				const auto* event_name = get_cell(events, event_row, 1);
				int kind{}, event{}, event_class{};
				if (!name || !*name || !predicate || *predicate || !event_name ||
					std::string_view{event_name} != binding.event_name ||
					!parse_integer(get_cell(challenges, row, 2), kind) || kind != 5 ||
					!parse_integer(get_cell(challenges, row, 3), event) || event != binding.event ||
					!parse_integer(get_cell(events, event_row, 2), event_class) || event_class != 0)
				{
					return false;
				}
				auto& record = result->records[index];
				record.name = name;
				record.kind = kind;
				record.progress = 1;
				record.progress_target = 1;
			}
			unsigned mask{};
			for (unsigned index = 0; index < maps.size(); ++index)
			{
				const auto row = find_row(chapters, 0, maps[index]);
				int chapter{};
				if (!parse_integer(get_cell(chapters, row, 1), chapter) || chapter < 1 || chapter > 3)
				{
					return false;
				}
				mask |= 1u << (first_map + index);
			}
			result->records[1].progress = static_cast<std::uint16_t>(mask);
			result->records[1].progress_target = mask;
			for (const auto& row : catalog->items)
			{
				if (row.reference_valid && row.item.reference == "Zombie_Tutorial_Level_Unlocked")
				{
					result->house_item = row.item.item_id;
				}
			}
			if (!result->house_item || catalog != loot_catalog::get_snapshot())
			{
				return false;
			}
			current.store(std::move(result)); // Owned values remain valid across map unloads.
			return true;
		}

		std::optional<std::uint64_t> parameter(const reward_game_events::event& event, const char* selector)
		{
			std::optional<std::uint64_t> value;
			for (const auto& entry : event.parameters)
			{
				if (entry.selector == selector)
				{
					if (value)
					{
						return std::nullopt;
					}
					value = entry.value;
				}
			}
			return value;
		}

		bool settle(const std::vector<achievement_record>& records, const std::uint32_t house_item,
			const std::uint64_t user)
		{
			std::function<bool(marketplace_store::transaction&)> reward;
			bool inventory_changed{};
			if (house_item)
			{
				reward = [&](marketplace_store::transaction& state)
				{
					auto item = state.get_inventory(house_item).value_or(marketplace_store::inventory_record{});
					if ((item.player_id && item.player_id != user) ||
						(!item.account_type.empty() && item.account_type != "steam") || item.collision_field ||
						!((!item.expire_date_time && !item.expiry_duration) ||
							(item.expire_date_time == UINT32_MAX && item.expiry_duration == INT64_MAX)))
					{
						return false;
					}
					if (item.quantity)
					{
						return true;
					}
					item.item_id = house_item;
					item.player_id = user;
					item.account_type = "steam";
					item.quantity = 1;
					item.mod_date_time = static_cast<std::uint32_t>(time(nullptr));
					inventory_changed = state.set_inventory(item) == marketplace_store::edit_result::updated;
					return inventory_changed;
				};
			}
			const auto result = achievement_store::merge_completion_bits(records, reward);
			if (result == achievement_store::mutation_result::save_failed)
			{
				return false;
			}
			if (result == achievement_store::mutation_result::updated)
			{
				achievement_sync::request_refresh();
			}
			if (inventory_changed)
			{
				economy::request_inventory_refresh();
			}
			return true;
		}
	}

	bool process(const demonware::reward_game_events::event& event, const std::uint64_t user)
	{
		if (!user || game::environment::is_dedicated() || !game::environment::is_zombies())
		{
			return true;
		}
		std::size_t index{};
		std::uint16_t bits = 1;
		const auto map = parameter(event, "5");
		if (event.name == "zombies" && parameter(event, "1") == 1)
		{
			index = 0; // killBoss
		}
		else if (event.name == "zombies_map_won" && map && *map >= first_map && *map < first_map + maps.size())
		{
			index = 1;
			bits = static_cast<std::uint16_t>(1u << *map);
		}
		// unlockzmshatteredmap emits [5,5] even on Thule. It is a sentinel here.
		else if (event.name == "zombies_dlc3_sv_unlock" && map == 5)
		{
			index = 2;
		}
		else if (event.name == "zombies_dlc3_ee_unlock" && map && *map >= first_map && *map < first_map + maps.size())
		{
			index = 3 + static_cast<std::size_t>(*map - first_map);
		}
		else if (event.name == "zombies_dlc3_skull_unlock" && map == 7)
		{
			index = 6;
		}
		else
		{
			return true;
		}
		const auto data = current.load();
		if (!data)
		{
			return false;
		}
		auto record = data->records[index];
		record.progress = bits;
		return settle({record}, index == 0 ? data->house_item : 0, user);
	}

	bool unlock_quests()
	{
		const auto data = current.load();
		const auto identity = demonware::runtime_context::get_snapshot();
		return data && identity && identity->user_id &&
			settle({data->records.begin(), data->records.end()}, data->house_item, identity->user_id);
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::is_dedicated() && game::environment::is_zombies())
			{
				scheduler::schedule(load_definitions, scheduler::pipeline::main, 1s);
			}
		}
	};
}

REGISTER_COMPONENT(zombies_progression::component)
