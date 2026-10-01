#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "zombies_progression.hpp"
#include "economy.hpp"
#include "component/achievement_sync.hpp"
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

		struct binding
		{
			int id;
			int event;
			const char* event_name;
		};

		constexpr std::array bindings{
			binding{344, 16, "zombies"},
			binding{1112, 41, "zombies_map_won"},
			binding{1114, 42, "zombies_dlc3_sv_unlock"},
			binding{759, 43, "zombies_dlc3_ee_unlock"},
			binding{758, 43, "zombies_dlc3_ee_unlock"},
			binding{760, 43, "zombies_dlc3_ee_unlock"},
			binding{761, 44, "zombies_dlc3_skull_unlock"},
		};

		constexpr std::size_t boss_index = 0;
		constexpr std::size_t map_won_index = 1;
		constexpr std::size_t survival_unlock_index = 2;
		constexpr std::size_t easter_egg_first_index = 3;
		constexpr std::size_t skull_unlock_index = 6;

		constexpr int challenge_kind = 5;
		constexpr int expected_event_class = 0;

		// Map selector values from ZMUtils.Value_Zm_Map_*, not chapter indices
		// Stock LUI tests hasbit(AE1112.progress, bit(mapValue + 1)), so 1 << mapValue
		constexpr std::array maps{"mp_zombie_windmill", "mp_zombie_dnk", "mp_zombie_dig_02"};
		constexpr unsigned first_map = 5;

		constexpr std::uint64_t survival_unlock_map = 5;
		constexpr std::uint64_t skull_unlock_map = 7;

		struct definitions
		{
			std::array<achievement_record, bindings.size()> records;
			std::uint32_t house_item{};
		};

		std::atomic<std::shared_ptr<const definitions>> current;

		bool read_binding(const binding& entry, const game::StringTable* challenges,
			const game::StringTable* events, achievement_record& record)
		{
			const auto row = find_row(challenges, 0, std::to_string(entry.id));
			const auto event_row = find_row(events, 0, std::to_string(entry.event));

			const auto* name = get_cell(challenges, row, 1);
			const auto* predicate = get_cell(challenges, row, 4);
			const auto* event_name = get_cell(events, event_row, 1);
			if (!name || !*name || !predicate || *predicate || !event_name ||
				std::string_view{event_name} != entry.event_name)
			{
				return false;
			}

			int kind{};
			int event{};
			int event_class{};
			if (!parse_integer(get_cell(challenges, row, 2), kind) || kind != challenge_kind ||
				!parse_integer(get_cell(challenges, row, 3), event) || event != entry.event ||
				!parse_integer(get_cell(events, event_row, 2), event_class) || event_class != expected_event_class)
			{
				return false;
			}

			record.name = name;
			record.kind = kind;
			record.progress = 1;
			record.progress_target = 1;
			return true;
		}

		bool read_map_mask(const game::StringTable* chapters, unsigned& mask)
		{
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

			return true;
		}

		std::uint32_t find_house_item(const loot_catalog::catalog& catalog)
		{
			std::uint32_t house_item{};
			for (const auto& row : catalog.items)
			{
				if (row.reference_valid && row.item.reference == "Zombie_Tutorial_Level_Unlocked")
				{
					house_item = row.item.item_id;
				}
			}

			return house_item;
		}

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
				if (!read_binding(bindings[index], challenges, events, result->records[index]))
				{
					return false;
				}
			}

			unsigned mask{};
			if (!read_map_mask(chapters, mask))
			{
				return false;
			}

			result->records[map_won_index].progress = static_cast<std::uint16_t>(mask);
			result->records[map_won_index].progress_target = mask;
			result->house_item = find_house_item(*catalog);
			if (!result->house_item || catalog != loot_catalog::get_snapshot())
			{
				return false;
			}

			current.store(std::move(result));
			return true;
		}

		std::optional<std::uint64_t> parameter(const reward_game_events::event& event, const char* selector)
		{
			std::optional<std::uint64_t> value;
			for (const auto& entry : event.parameters)
			{
				if (entry.selector != selector)
				{
					continue;
				}

				if (value)
				{
					return std::nullopt;
				}

				value = entry.value;
			}

			return value;
		}

		bool has_valid_expiry(const marketplace_store::inventory_record& item)
		{
			const auto never_expires = !item.expire_date_time && !item.expiry_duration;
			const auto max_expiry = item.expire_date_time == UINT32_MAX && item.expiry_duration == INT64_MAX;
			return never_expires || max_expiry;
		}

		bool conflicts_with_grant(const marketplace_store::inventory_record& item, const std::uint64_t user)
		{
			return (item.player_id && item.player_id != user) ||
				(!item.account_type.empty() && item.account_type != "steam") ||
				item.collision_field || !has_valid_expiry(item);
		}

		bool grant_house_item(marketplace_store::transaction& state, const std::uint32_t house_item,
			const std::uint64_t user, bool& inventory_changed)
		{
			auto item = state.get_inventory(house_item).value_or(marketplace_store::inventory_record{});
			if (conflicts_with_grant(item, user))
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
					return grant_house_item(state, house_item, user, inventory_changed);
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

		bool is_valid_map(const std::optional<std::uint64_t>& map)
		{
			return map && *map >= first_map && *map < first_map + maps.size();
		}

		struct progress_target
		{
			std::size_t index{};
			std::uint16_t bits{1};
		};

		std::optional<progress_target> resolve_target(const reward_game_events::event& event)
		{
			const auto map = parameter(event, "5");

			if (event.name == "zombies" && parameter(event, "1") == 1)
			{
				return progress_target{boss_index};
			}

			if (event.name == "zombies_map_won" && is_valid_map(map))
			{
				return progress_target{map_won_index, static_cast<std::uint16_t>(1u << *map)};
			}

			// unlockzmshatteredmap emits [5,5] even on Thule, so this is only a sentinel
			if (event.name == "zombies_dlc3_sv_unlock" && map == survival_unlock_map)
			{
				return progress_target{survival_unlock_index};
			}

			if (event.name == "zombies_dlc3_ee_unlock" && is_valid_map(map))
			{
				return progress_target{easter_egg_first_index + static_cast<std::size_t>(*map - first_map)};
			}

			if (event.name == "zombies_dlc3_skull_unlock" && map == skull_unlock_map)
			{
				return progress_target{skull_unlock_index};
			}

			return std::nullopt;
		}
	}

	bool process(const demonware::reward_game_events::event& event, const std::uint64_t user)
	{
		if (!user || game::environment::is_dedicated() || !game::environment::is_zombies())
		{
			return true;
		}

		const auto target = resolve_target(event);
		if (!target)
		{
			return true;
		}

		const auto data = current.load();
		if (!data)
		{
			return false;
		}

		auto record = data->records[target->index];
		record.progress = target->bits;

		return settle({record}, target->index == boss_index ? data->house_item : 0, user);
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
