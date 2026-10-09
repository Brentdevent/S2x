#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "hidden_challenges.hpp"

#include "component/achievement_sync.hpp"
#include "component/console/console.hpp"
#include "component/scheduler.hpp"

#include "game/game.hpp"
#include "game/string_table.hpp"
#include "game/demonware/achievement/store.hpp"
#include "game/demonware/reward/game_event.hpp"

#include <mutex>
#include <unordered_map>

namespace hidden_challenges
{
	using reward_game_event = demonware::reward_game_events::event;

	namespace
	{
		using game::string_table::get_cell;
		using game::string_table::parse_integer;
		using game::string_table::find_row;
		using game::string_table::split_references;

		constexpr auto hidden_challenge_event_id = 16;
		constexpr std::string_view hidden_challenge_event_name = "zombies";
		constexpr auto hidden_challenge_kind = 5;

		constexpr auto reference_column = 0;
		constexpr auto category_challenges_column = 4;
		constexpr auto achievement_id_column = 5;
		constexpr auto challenge_bit_column = 5;

		constexpr auto definition_id_column = 0;
		constexpr auto definition_name_column = 1;
		constexpr auto definition_kind_column = 2;
		constexpr auto definition_event_column = 3;
		constexpr auto definition_predicate_column = 4;

		constexpr auto event_id_column = 0;
		constexpr auto event_name_column = 1;
		constexpr auto event_class_column = 2;

		struct hidden_group
		{
			int value;
			int achievement_id;
		};

		// Selector 3 contains the stock hidden-character group value. The parent AE IDs
		// join the shipped tables; the comments name the stock script groups.
		// Group 22 is the radio camo, handled by zombies_progression. Selector 4 is a
		// zero-based challenge slot within the resolved character group.
		constexpr std::array hidden_groups
		{
			hidden_group{1, 363}, // treasure_set
			hidden_group{2, 365}, // raven_set
			hidden_group{3, 366}, // assassin_set
			hidden_group{4, 367}, // survivalist_set
			hidden_group{5, 368}, // mountain_man_set
			hidden_group{6, 369}, // bat_elite_set
			hidden_group{7, 64}, // survivalist_origin_set
			hidden_group{8, 65}, // survivalist_bat_set
			hidden_group{9, 66}, // survivalist_blood_set
			hidden_group{10, 67}, // hunter_origin_set
			hidden_group{11, 68}, // hunter_bat_set
			hidden_group{12, 69}, // hunter_blood_set
			hidden_group{13, 70}, // mountain_man_origin_set
			hidden_group{14, 71}, // mountain_man_bat_set
			hidden_group{15, 72}, // mountain_man_blood_set
			hidden_group{16, 73}, // assassin_origin_set
			hidden_group{17, 74}, // assassin_bat_set
			hidden_group{18, 75}, // assassin_blood_set
			hidden_group{19, 350}, // surgeon_set
			hidden_group{20, 351}, // rebel_set
			hidden_group{21, 568}, // super_soldier_set
			hidden_group{23, 1096}, // arrow_set
			hidden_group{24, 1097}, // captain_set
			hidden_group{25, 1098}, // explorer_set
			hidden_group{26, 1136}, // african_set
			hidden_group{27, 1137}, // outlaw_set
			hidden_group{28, 1138}, // arabic_set
			hidden_group{29, 1141}, // wicht_set
		};

		struct hidden_challenge_definition
		{
			std::string achievement_name{};
			int achievement_kind{};
			std::uint16_t full_mask{};
		};

		std::atomic_bool accepting_events{};
		std::mutex pending_event_mutex{};
		// Completions are idempotent bits, not an unbounded stream of work. Keep
		// one mask per known group, including events waiting for assets or a save retry.
		using completion_masks = std::array<std::uint16_t, hidden_groups.size()>;
		completion_masks pending_events{};
		std::unordered_map<int, hidden_challenge_definition> definitions{};
		std::size_t last_reported_definition_count{std::numeric_limits<std::size_t>::max()};
		std::chrono::steady_clock::time_point next_definition_load{};
		std::chrono::steady_clock::time_point next_event_retry{};

		std::size_t get_group_index(const std::uint64_t value)
		{
			const auto group = std::find_if(hidden_groups.begin(), hidden_groups.end(),
				[value](const hidden_group& entry) { return entry.value == value; });
			return static_cast<std::size_t>(group - hidden_groups.begin());
		}

		int find_category_row(const game::StringTable* table, const int achievement_id)
		{
			for (auto row = 0; table && row < table->rowCount; ++row)
			{
				const auto* reference = get_cell(table, row, reference_column);
				int row_achievement_id{};
				if (!reference || !std::string_view{reference}.starts_with("category"))
				{
					continue;
				}

				if (parse_integer(get_cell(table, row, achievement_id_column), row_achievement_id) &&
					row_achievement_id == achievement_id)
				{
					return row;
				}
			}

			return -1;
		}

		bool get_category_mask(const game::StringTable* table, const int category_row,
			std::uint16_t& full_mask)
		{
			const auto challenges = split_references(get_cell(table, category_row,
				category_challenges_column));
			if (challenges.empty())
			{
				return false;
			}

			std::uint32_t mask{};
			for (const auto challenge : challenges)
			{
				const auto challenge_row = find_row(table, reference_column, challenge);
				int bit{};
				if (challenge_row < 0 ||
					!parse_integer(get_cell(table, challenge_row, challenge_bit_column), bit) ||
					bit <= 0 || bit > std::numeric_limits<std::uint16_t>::digits)
				{
					return false;
				}

				const auto bit_mask = 1u << (bit - 1);
				if ((mask & bit_mask) != 0)
				{
					return false;
				}

				mask |= bit_mask;
			}

			if ((mask != 7 && mask != 31) ||
				mask > std::numeric_limits<std::uint16_t>::max())
			{
				return false;
			}

			full_mask = static_cast<std::uint16_t>(mask);
			return true;
		}

		bool build_definition(const hidden_group& group, const game::StringTable* challenges,
			const game::StringTable* achievement_definitions, const game::StringTable* game_events,
			hidden_challenge_definition& result)
		{
			const auto category_row = find_category_row(challenges, group.achievement_id);
			std::uint16_t full_mask{};
			if (category_row < 0 ||
				!get_category_mask(challenges, category_row, full_mask))
			{
				return false;
			}

			const auto definition_row = find_row(achievement_definitions, definition_id_column,
				std::to_string(group.achievement_id));
			int kind{};
			int event_id{};
			if (definition_row < 0 ||
				!parse_integer(get_cell(achievement_definitions, definition_row,
					definition_kind_column), kind) ||
				!parse_integer(get_cell(achievement_definitions, definition_row,
					definition_event_column), event_id) ||
				kind != hidden_challenge_kind || event_id != hidden_challenge_event_id)
			{
				return false;
			}

			const auto* predicate = get_cell(achievement_definitions, definition_row,
				definition_predicate_column);
			const auto* achievement_name = get_cell(achievement_definitions, definition_row,
				definition_name_column);
			if ((predicate && *predicate) || !achievement_name || !*achievement_name ||
				!std::string_view{achievement_name}.ends_with("_gear_bitfield_zm"))
			{
				return false;
			}

			const auto event_row = find_row(game_events, event_id_column, std::to_string(event_id));
			int event_class{};
			const auto* event_name = get_cell(game_events, event_row, event_name_column);
			if (event_row < 0 || !event_name || hidden_challenge_event_name != event_name ||
				!parse_integer(get_cell(game_events, event_row, event_class_column), event_class) ||
				event_class != 0)
			{
				return false;
			}

			result.achievement_name = achievement_name;
			result.achievement_kind = kind;
			result.full_mask = full_mask;
			return true;
		}

		void load_definitions()
		{
			if (definitions.size() == hidden_groups.size())
			{
				return;
			}

			const auto now = std::chrono::steady_clock::now();
			if (now < next_definition_load)
			{
				return;
			}

			next_definition_load = now + 1s;
			const auto* challenges = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,
				"mp/zombieCostumeChallenges.csv", false).stringTable;
			const auto* achievement_definitions = game::DB_FindXAssetHeader(
				game::ASSET_TYPE_STRINGTABLE, "dw/dwGameChallenges.csv", false).stringTable;
			const auto* game_events = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,
				"dw/dwGameEvents.csv", false).stringTable;
			if (!challenges || !achievement_definitions || !game_events)
			{
				return;
			}

			if (challenges->rowCount <= 0 || achievement_definitions->rowCount <= 0 ||
				game_events->rowCount <= 0)
			{
				return;
			}

			for (const auto& group : hidden_groups)
			{
				if (definitions.contains(group.value))
				{
					continue;
				}

				hidden_challenge_definition definition{};
				if (build_definition(group, challenges, achievement_definitions, game_events, definition))
				{
					definitions.emplace(group.value, std::move(definition));
				}
			}

			if (definitions.size() != last_reported_definition_count)
			{
				console::debug("[DW] hidden_challenges: loaded %zu of %zu character groups\n",
					definitions.size(), hidden_groups.size());
				last_reported_definition_count = definitions.size();
			}
		}

		bool get_parameter(const reward_game_event& event, const std::string_view selector,
			std::uint64_t& value)
		{
			bool found{};
			for (const auto& parameter : event.parameters)
			{
				if (parameter.selector != selector)
				{
					continue;
				}

				if (found)
				{
					return false;
				}

				found = true;
				value = parameter.value;
			}

			return found;
		}

		bool get_hidden_challenge_values(const reward_game_event& event,
			std::uint64_t& group_value, std::uint64_t& challenge_value)
		{
			return event.name == hidden_challenge_event_name &&
				get_parameter(event, "3", group_value) &&
				get_parameter(event, "4", challenge_value) &&
				get_group_index(group_value) != hidden_groups.size() &&
				challenge_value < std::numeric_limits<std::uint16_t>::digits;
		}

		demonware::achievement_store::mutation_result update_progress(
			const hidden_challenge_definition& definition, const std::uint16_t challenge_mask)
		{
			std::uint16_t previous_progress{};
			std::uint16_t updated_progress{};
			const auto result = demonware::achievement_store::mutate(definition.achievement_name,
				[&](demonware::achievement_record& record)
				{
					previous_progress = record.progress;
					updated_progress = static_cast<std::uint16_t>(
						(record.progress & definition.full_mask) | challenge_mask);
					const auto completed = updated_progress == definition.full_mask;
					const auto status = completed
						? demonware::achievement_status::finished
						: demonware::achievement_status::in_progress;
					const auto fulfilled_times = completed ? std::max(record.fulfilled_times, 1) : 0;
					const auto completion_timestamp = completed
						? (record.completion_timestamp
							? record.completion_timestamp
							: static_cast<std::uint64_t>(time(nullptr)))
						: 0;

					const auto changed = record.kind != definition.achievement_kind ||
						record.progress != updated_progress ||
						record.progress_target != definition.full_mask ||
						record.fulfilled_times != fulfilled_times ||
						record.completion_timestamp != completion_timestamp ||
						record.status != status;
					record.kind = definition.achievement_kind;
					record.progress = updated_progress;
					record.progress_target = definition.full_mask;
					record.fulfilled_times = fulfilled_times;
					record.completion_timestamp = completion_timestamp;
					record.status = status;
					return changed;
				});

			if (result == demonware::achievement_store::mutation_result::save_failed)
			{
				console::error("[DW] hidden_challenges: failed to persist %s\n",
					definition.achievement_name.data());
				return result;
			}

			if (result == demonware::achievement_store::mutation_result::updated)
			{
				console::debug("[DW] hidden_challenges: %s: 0x%02X -> 0x%02X\n",
					definition.achievement_name.data(), previous_progress, updated_progress);
			}
			return result;
		}

		void process_pending_events()
		{
			load_definitions();
			const auto now = std::chrono::steady_clock::now();
			if (now < next_event_retry)
			{
				return;
			}

			completion_masks events{};
			{
				std::lock_guard lock{pending_event_mutex};
				events.swap(pending_events);
			}

			bool changed{};
			bool retry{};
			for (std::size_t index = 0; index < events.size(); ++index)
			{
				if (!events[index])
				{
					continue;
				}

				const auto definition = definitions.find(hidden_groups[index].value);
				if (definition == definitions.end())
				{
					retry = true;
					continue;
				}

				events[index] &= definition->second.full_mask;
				if (!events[index])
				{
					continue;
				}

				const auto result = update_progress(definition->second, events[index]);
				if (result == demonware::achievement_store::mutation_result::save_failed)
				{
					retry = true;
					continue;
				}

				changed |= result == demonware::achievement_store::mutation_result::updated;
				events[index] = 0;
			}

			if (retry)
			{
				next_event_retry = now + 1s;
				std::lock_guard lock{pending_event_mutex};
				for (std::size_t index = 0; index < events.size(); ++index)
				{
					pending_events[index] |= events[index];
				}
			}

			if (changed)
			{
				achievement_sync::request_refresh();
			}
		}

		void clear_pending_events()
		{
			std::lock_guard lock{pending_event_mutex};
			pending_events.fill(0);
		}
	}

	bool get_completion(const reward_game_event& event, std::uint32_t& group,
		std::uint32_t& challenge)
	{
		std::uint64_t group_value{};
		std::uint64_t challenge_value{};
		if (!get_hidden_challenge_values(event, group_value, challenge_value))
		{
			return false;
		}

		group = static_cast<std::uint32_t>(group_value);
		challenge = static_cast<std::uint32_t>(challenge_value);
		return true;
	}

	void submit_completion(const std::uint32_t group, const std::uint32_t challenge)
	{
		const auto index = get_group_index(group);
		if (!accepting_events.load() || index == hidden_groups.size() ||
			challenge >= std::numeric_limits<std::uint16_t>::digits)
		{
			return;
		}

		std::lock_guard lock{pending_event_mutex};
		if (!accepting_events.load())
		{
			return;
		}

		pending_events[index] |= static_cast<std::uint16_t>(1u << challenge);
	}

	void submit_reward_game_event(reward_game_event event)
	{
		std::uint32_t group{};
		std::uint32_t challenge{};
		if (get_completion(event, group, challenge))
		{
			submit_completion(group, challenge);
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated() || !game::environment::is_zombies())
			{
				return;
			}

			accepting_events = true;
			scheduler::loop(process_pending_events, scheduler::pipeline::main, 50ms);
		}

		void pre_destroy() override
		{
			accepting_events = false;
			clear_pending_events();
		}
	};
}

REGISTER_COMPONENT(hidden_challenges::component)
