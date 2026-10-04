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
#include "game/demonware/supply_drop_inventory.hpp"

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
			int group{};
			const char* reward_reference{};
		};

		constexpr std::array bindings{
			binding{344, 16, "zombies", 0, "Zombie_Tutorial_Level_Unlocked"},
			binding{1112, 41, "zombies_map_won"},
			binding{1114, 42, "zombies_dlc3_sv_unlock"},
			binding{759, 43, "zombies_dlc3_ee_unlock"},
			binding{758, 43, "zombies_dlc3_ee_unlock"},
			binding{760, 43, "zombies_dlc3_ee_unlock"},
			binding{761, 44, "zombies_dlc3_skull_unlock"},
			// Native event 16, selector 3: radio camo, prestige milestones and all character challenges.
			// Local reward bindings restore the backend grants; IDs come from the native catalog.
			binding{83, 16, "zombies", 22},
			binding{1142, 16, "zombies", 30, "weaponcharm_mtx8_01"},
			binding{1143, 16, "zombies", 31, "weaponcharm_dlc4_01"},
			binding{1144, 16, "zombies", 32, "zom_stun_01"},
			binding{1145, 16, "zombies", 33},
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

		struct rank_reward
		{
			achievement_record record;
			int level{};
			int experience{};
			std::uint32_t item{};
		};

		struct definitions
		{
			std::array<achievement_record, bindings.size()> records;
			std::array<std::uint32_t, bindings.size()> rewards{};
			std::vector<rank_reward> ranks;
			int max_prestige{};
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

		std::uint32_t find_reward_item(const loot_catalog::catalog& catalog, const char* reference)
		{
			for (const auto& row : catalog.items)
			{
				if (row.reference_valid && row.item.reference == reference)
				{
					return row.item.item_id;
				}
			}

			return 0;
		}

		bool read_rank_rewards(definitions& result, const game::StringTable* ranks,
			const game::StringTable* challenges, const game::StringTable* events, const loot_catalog::catalog& catalog)
		{
			if (!ranks || !ranks->values || ranks->columnCount < 20 ||
				!parse_integer(get_cell(ranks, find_row(ranks, 0, "maxprestige"), 1), result.max_prestige) ||
				result.max_prestige < 0 || result.max_prestige >= INT_MAX)
			{
				return false;
			}

			// ZMCacUtils maps SupplyDrop 1/2 to normal/rare; the label "Epic" in
			// its enum still presents advanced_zombie_supply_drop, not sd_zombie_epic.
			constexpr std::array drops{"sd_zombie", "sd_zombie_rare"};
			for (int row = 0; row < ranks->rowCount; ++row)
			{
				int index{};
				const auto* type = get_cell(ranks, row, 19);
				if (!parse_integer(get_cell(ranks, row, 0), index) || index < 0 || !type || !*type)
				{
					continue;
				}

				rank_reward reward;
				int drop_type{};
				if (!parse_integer(type, drop_type) || drop_type < 1 || drop_type > drops.size() ||
					!parse_integer(get_cell(ranks, row, 12), reward.level) || reward.level <= 0 ||
					!parse_integer(get_cell(ranks, row, 2), reward.experience) || reward.experience < 0)
				{
					return false;
				}

				const auto name = "player_zm_level_" + std::to_string(reward.level);
				int id{};
				const auto drop = loot_catalog::find_supply_drop(catalog, drops[drop_type - 1]);
				if (!drop || !parse_integer(get_cell(challenges, find_row(challenges, 1, name), 0), id) ||
					!read_binding({id, 14, "player_rank_up"}, challenges, events, reward.record))
				{
					return false;
				}

				reward.record.fulfilled_times = 0;
				reward.item = drop->item_id;
				result.ranks.push_back(std::move(reward));
			}

			return !result.ranks.empty();
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

			for (std::size_t index = 0; index < bindings.size(); ++index)
			{
				if (const auto* reference = bindings[index].reward_reference)
				{
					result->rewards[index] = find_reward_item(*catalog, reference);
					if (!result->rewards[index])
					{
						return false;
					}
				}
			}

			const auto* ranks = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,
				"mp/cp_rankTable.csv", false).stringTable;
			if (!read_rank_rewards(*result, ranks, challenges, events, *catalog))
			{
				return false;
			}

			unsigned mask{};
			if (!read_map_mask(chapters, mask))
			{
				return false;
			}

			result->records[map_won_index].progress = static_cast<std::uint16_t>(mask);
			result->records[map_won_index].progress_target = mask;
			if (catalog != loot_catalog::get_snapshot())
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

		bool process_rank(const reward_game_events::event& event, const std::uint64_t user)
		{
			const auto level = parameter(event, "1");
			const auto master = parameter(event, "3");
			if (parameter(event, "2") != 2 || !level || (master && *master != 0) ||
				std::ranges::count(event.parameters, "3", &reward_game_events::parameter::selector) > 1)
			{
				return true;
			}

			const auto data = current.load();
			const auto identity = runtime_context::get_snapshot();
			if (!data || !identity || identity->user_id != user || !identity->zombies)
			{
				return false;
			}

			const auto reward = std::ranges::find(data->ranks, *level, &rank_reward::level);
			if (reward == data->ranks.end())
			{
				return true;
			}

			const auto& rank = *identity->zombies;
			if (rank.prestige < 0 || rank.prestige > data->max_prestige || rank.experience < 0)
			{
				return false;
			}

			// The event has no prestige or occurrence ID. Bound fulfillment by the
			// native profile: once per completed prestige, plus the current earned
			// level. Stock prestige backfill and transport replays share this limit.
			const auto earned = rank.prestige + (rank.experience >= reward->experience ? 1 : 0);
			if (!earned)
			{
				// Stats replication/main-thread publication may follow the event.
				return false;
			}

			int grants{};
			const auto now = static_cast<std::uint32_t>(time(nullptr));
			const auto result = achievement_store::mutate(reward->record.name, [&](achievement_record& record)
			{
				if (record.kind != challenge_kind)
				{
					record = reward->record;
				}

				if (record.fulfilled_times >= earned)
				{
					return false;
				}

				grants = earned - record.fulfilled_times;
				record.fulfilled_times = earned;
				record.progress = 1;
				record.status = achievement_status::finished;
				record.completion_timestamp = now;
				return true;
			}, [&](marketplace_store::transaction& state)
			{
				marketplace_store::inventory_record item;
				for (int count = 0; count < grants; ++count)
				{
					if (!supply_drop_inventory::grant(state, reward->item, user, now, item))
					{
						return false;
					}
				}

				return true;
			});

			if (result == achievement_store::mutation_result::save_failed)
			{
				return false;
			}

			if (result == achievement_store::mutation_result::updated)
			{
				achievement_sync::request_refresh();
			}

			// Fetch current absolute quantities, including on acknowledgement retry.
			economy::request_inventory_refresh();
			return true;
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

		bool grant_reward_item(marketplace_store::transaction& state, const std::uint32_t reward_item,
			const std::uint64_t user, bool& inventory_changed)
		{
			auto item = state.get_inventory(reward_item).value_or(marketplace_store::inventory_record{});
			if (conflicts_with_grant(item, user))
			{
				return false;
			}

			if (item.quantity)
			{
				return true;
			}

			item.item_id = reward_item;
			item.player_id = user;
			item.account_type = "steam";
			item.quantity = 1;
			item.mod_date_time = static_cast<std::uint32_t>(time(nullptr));

			inventory_changed = state.set_inventory(item) == marketplace_store::edit_result::updated;
			return inventory_changed;
		}

		bool settle(const std::vector<achievement_record>& records, const std::vector<std::uint32_t>& reward_items,
			const std::uint64_t user)
		{
			std::function<bool(marketplace_store::transaction&)> reward;
			bool inventory_changed{};

			if (std::ranges::any_of(reward_items, [](const auto item) { return item != 0; }))
			{
				reward = [&](marketplace_store::transaction& state)
				{
					for (const auto item : reward_items)
					{
						if (item && !grant_reward_item(state, item, user, inventory_changed))
						{
							return false;
						}
					}

					return true;
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

			if (event.name == "zombies")
			{
				if (parameter(event, "1") == 1)
				{
					return progress_target{boss_index};
				}

				const auto group = parameter(event, "3");
				for (std::size_t index = 0; index < bindings.size(); ++index)
				{
					if (bindings[index].group && group == bindings[index].group)
					{
						return progress_target{index};
					}
				}
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

		if (event.name == "player_rank_up")
		{
			return process_rank(event, user);
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

		return settle({record}, {data->rewards[target->index]}, user);
	}

	bool unlock_quests()
	{
		const auto data = current.load();
		const auto identity = demonware::runtime_context::get_snapshot();

		return data && identity && identity->user_id &&
			settle({data->records.begin(), data->records.begin() + skull_unlock_index + 1},
				{data->rewards[boss_index]}, identity->user_id);
	}

	bool unlock_challenges(const std::vector<demonware::achievement_record>& records)
	{
		const auto data = current.load();
		const auto identity = demonware::runtime_context::get_snapshot();
		if (!data || !identity || !identity->user_id)
		{
			return false;
		}

		std::vector<std::uint32_t> rewards;
		for (std::size_t index = 0; index < data->records.size(); ++index)
		{
			if (data->rewards[index] && std::ranges::find(records, data->records[index].name,
				&demonware::achievement_record::name) != records.end())
			{
				rewards.push_back(data->rewards[index]);
			}
		}

		return settle(records, rewards, identity->user_id);
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
