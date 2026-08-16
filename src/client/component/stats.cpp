#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console/console.hpp"
#include "unlock_zombies.hpp"

#include "game/game.hpp"

#include <algorithm>
#include <charconv>

namespace stats
{
	namespace
	{
		constexpr unsigned int ranked_stats_group = 0;
		constexpr unsigned int stats_group_count = 10;
		constexpr std::size_t max_stat_path_elements = 16;
		// Division levels are stored zero-based: rank 3 is displayed as level 4.
		constexpr int max_division_rank = 3;
		constexpr int max_division_prestige = 4;
		constexpr int max_weapon_prestige = 4;
		constexpr int challenge_target_column = 9;
		constexpr int max_challenge_tiers = 9;

		struct alignas(16) ddl_state
		{
			std::byte data[32]{};
		};

		struct weapon_progress
		{
			int level{};
			int experience{};
		};

		bool parse_integer(const char* text, int& value)
		{
			if (!text || !*text)
			{
				return false;
			}

			const auto* end = text + std::strlen(text);
			const auto result = std::from_chars(text, end, value);
			return result.ec == std::errc{} && result.ptr == end;
		}

		const char* get_cell(const game::StringTable* table, const int row, const int column)
		{
			if (!table || !table->values || row < 0 || row >= table->rowCount || column < 0 ||
				column >= table->columnCount)
			{
				return nullptr;
			}

			return table->values[row * table->columnCount + column].string;
		}

		game::StringTable* find_string_table(const char* name)
		{
			return game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE, name, false).stringTable;
		}

		int find_row(const game::StringTable* table, const int column, const std::string_view value)
		{
			const std::string needle{value};

			for (auto row = 0; table && row < table->rowCount; ++row)
			{
				const auto* cell = get_cell(table, row, column);
				if (cell && _stricmp(cell, needle.c_str()) == 0)
				{
					return row;
				}
			}

			return -1;
		}

		bool get_integer_cell(const game::StringTable* table, const int row, const int column, int& value)
		{
			return parse_integer(get_cell(table, row, column), value);
		}

		bool has_stats()
		{
			const auto controller_index = game::CL_ControllerIndexFromClientNum(0);
			return controller_index >= 0 && game::LiveStorage_DoWeHaveStats(controller_index);
		}

		bool is_stat_path_valid(const unsigned int* path, const std::size_t path_count,
			const unsigned int stats_group)
		{
			if (!path || path_count == 0 || path_count > max_stat_path_elements)
			{
				return false;
			}

			const auto* definition = game::LiveStorage_GetStatsGroupDDLDefinition(stats_group);
			if (!definition)
			{
				return false;
			}

			ddl_state state{};
			game::DDL_InitState(definition, &state, stats_group);

			if (!game::DDL_MoveToPath(&state, &state, static_cast<int>(path_count), path))
			{
				return false;
			}

			return game::DDL_GetType(&state) <= 3;
		}

		bool set_stat(const unsigned int* path, const std::size_t path_count, const int value,
			const unsigned int stats_group)
		{
			const auto controller_index = game::CL_ControllerIndexFromClientNum(0);
			if (controller_index < 0 || !game::LiveStorage_DoWeHaveStats(controller_index) || !path ||
				path_count == 0 || path_count > std::numeric_limits<unsigned int>::max() ||
				!is_stat_path_valid(path, path_count, stats_group))
			{
				return false;
			}

			if (!game::LiveStorage_PlayerDataSetIntByNameArray(controller_index, path,
				static_cast<unsigned int>(path_count), value, stats_group))
			{
				return false;
			}

			game::LiveStorage_StatsWriteNeeded(controller_index);
			return true;
		}

		bool set_ranked_stat(const unsigned int* path, const std::size_t path_count, const int value)
		{
			return set_stat(path, path_count, value, ranked_stats_group);
		}

		bool set_stat(const std::initializer_list<std::string_view> path, const int value,
			const unsigned int stats_group)
		{
			std::vector<unsigned int> hashed_path{};
			hashed_path.reserve(path.size());

			for (const auto token : path)
			{
				if (token.empty())
				{
					return false;
				}

				const std::string null_terminated_token{token};
				hashed_path.emplace_back(game::DDL_HashString(null_terminated_token.c_str()));
			}

			return set_stat(hashed_path.data(), hashed_path.size(), value, stats_group);
		}

		bool set_ranked_stat(const std::initializer_list<std::string_view> path, const int value)
		{
			return set_stat(path, value, ranked_stats_group);
		}

		bool is_stat_path_valid(const std::initializer_list<std::string_view> path,
			const unsigned int stats_group)
		{
			std::vector<unsigned int> hashed_path{};
			hashed_path.reserve(path.size());

			for (const auto token : path)
			{
				if (token.empty())
				{
					return false;
				}

				const std::string null_terminated_token{token};
				hashed_path.emplace_back(game::DDL_HashString(null_terminated_token.c_str()));
			}

			return is_stat_path_valid(hashed_path.data(), hashed_path.size(), stats_group);
		}

		bool find_zombie_stats_group(unsigned int& stats_group)
		{
			for (auto candidate = 0u; candidate < stats_group_count; ++candidate)
			{
				if (is_stat_path_valid({"prestigeLevel"}, candidate) &&
					is_stat_path_valid({"totalXP"}, candidate))
				{
					stats_group = candidate;
					return true;
				}
			}

			return false;
		}

		bool set_ranked_stat_with_leaf(const std::initializer_list<std::string_view> parent_path,
			const std::initializer_list<std::string_view> leaf_candidates, const int value)
		{
			std::vector<unsigned int> hashed_path{};
			hashed_path.reserve(parent_path.size() + 1);

			for (const auto token : parent_path)
			{
				if (token.empty())
				{
					return false;
				}

				const std::string null_terminated_token{token};
				hashed_path.emplace_back(game::DDL_HashString(null_terminated_token.c_str()));
			}

			for (const auto leaf : leaf_candidates)
			{
				const std::string null_terminated_leaf{leaf};
				hashed_path.emplace_back(game::DDL_HashString(null_terminated_leaf.c_str()));

				if (set_ranked_stat(hashed_path.data(), hashed_path.size(), value))
				{
					return true;
				}

				hashed_path.pop_back();
			}

			return false;
		}

		bool get_rank_caps(const char* table_name, int& max_prestige, int& max_experience)
		{
			const auto* rank_table = find_string_table(table_name);
			if (!rank_table)
			{
				return false;
			}

			const auto max_prestige_row = find_row(rank_table, 0, "maxprestige");
			const auto final_rank_row = find_row(rank_table, 0, "maxrankfinalprestige");
			int final_rank{};

			if (!get_integer_cell(rank_table, max_prestige_row, 1, max_prestige) ||
				!get_integer_cell(rank_table, final_rank_row, 1, final_rank))
			{
				return false;
			}

			const auto experience_row = find_row(rank_table, 0, std::to_string(final_rank));
			return get_integer_cell(rank_table, experience_row, 7, max_experience);
		}

		void add_weapon_progress(const game::StringTable* table,
			std::map<std::string, weapon_progress>& weapons,
			const int level_cap = std::numeric_limits<int>::max())
		{
			for (auto row = 0; table && row < table->rowCount; ++row)
			{
				const auto* weapon = get_cell(table, row, 0);
				int max_level{};

				if (!weapon || !*weapon || !get_integer_cell(table, row, 1, max_level) || max_level <= 0 ||
					max_level > table->rowCount - row - 1)
				{
					continue;
				}

				const auto target_level = std::min(max_level, level_cap);
				int max_experience{};
				if (target_level <= 0 ||
					!get_integer_cell(table, row + target_level, 1, max_experience) || max_experience < 0 ||
					max_experience == std::numeric_limits<int>::max())
				{
					continue;
				}

				auto& progress = weapons[weapon];
				if (target_level > progress.level ||
					(target_level == progress.level && max_experience + 1 > progress.experience))
				{
					progress.level = target_level;
					// The stock rank-up path stores one past the threshold for the current level.
					progress.experience = max_experience + 1;
				}
			}
		}

		std::map<std::string, weapon_progress> get_weapon_progress()
		{
			std::map<std::string, weapon_progress> weapons{};

			add_weapon_progress(find_string_table("mp/weaponLeveling.csv"), weapons);
			add_weapon_progress(find_string_table("mp/weaponLevelingDivisionsOverhaul.csv"), weapons);

			return weapons;
		}

		weapon_progress get_division_progress()
		{
			std::map<std::string, weapon_progress> entries{};
			const auto* overhaul_table = find_string_table("mp/divisionLevelingOverhaul.csv");
			const auto* original_table = find_string_table("mp/divisionLeveling.csv");

			add_weapon_progress(overhaul_table, entries, max_division_rank);
			add_weapon_progress(original_table, entries, max_division_rank);

			weapon_progress result{};
			for (const auto& [name, progress] : entries)
			{
				(void)name;

				if (progress.level > max_division_rank)
				{
					continue;
				}

				if (progress.level > result.level ||
					(progress.level == result.level && progress.experience > result.experience))
				{
					result = progress;
				}
			}

			for (const auto* table : {overhaul_table, original_table})
			{
				for (auto row = 0; table && row < table->rowCount; ++row)
				{
					int level{};
					int experience{};
					if (!get_integer_cell(table, row, 0, level) ||
						!get_integer_cell(table, row, 1, experience) || level <= 0 ||
						level > max_division_rank ||
						experience < 0 || experience == std::numeric_limits<int>::max())
					{
						continue;
					}

					if (level > result.level || (level == result.level && experience + 1 > result.experience))
					{
						result.level = level;
						result.experience = experience + 1;
					}
				}
			}

			return result;
		}

		std::vector<std::string> get_weapon_stat_names(const std::string& weapon)
		{
			std::vector<std::string> names{weapon};
			constexpr std::string_view mp_suffix{"_mp"};

			if (weapon.size() > mp_suffix.size() &&
				weapon.compare(weapon.size() - mp_suffix.size(), mp_suffix.size(), mp_suffix) == 0)
			{
				names.emplace_back(weapon.substr(0, weapon.size() - mp_suffix.size()));
			}
			else
			{
				names.emplace_back(weapon + std::string{mp_suffix});
			}

			return names;
		}

		bool set_weapon_stat(const std::string& weapon,
			const std::initializer_list<std::string_view> leaf_candidates, const int value)
		{
			for (const auto& stat_name : get_weapon_stat_names(weapon))
			{
				if (set_ranked_stat_with_leaf({"weaponStats", stat_name}, leaf_candidates, value))
				{
					return true;
				}
			}

			return false;
		}

		std::pair<std::size_t, std::size_t> unlock_challenges()
		{
			const auto* table = find_string_table("mp/allChallengesTable.csv");
			std::size_t updated{};
			std::size_t total{};

			for (auto row = 0; table && row < table->rowCount; ++row)
			{
				const auto* challenge = get_cell(table, row, 0);
				if (!challenge || !*challenge)
				{
					continue;
				}

				++total;

				int max_state{};
				int max_progress{};
				for (auto tier = 0; tier < max_challenge_tiers; ++tier)
				{
					int progress{};
					if (!get_integer_cell(table, row, challenge_target_column + tier * 2, progress) ||
						progress == 0)
					{
						break;
					}

					// State 1 is the active first tier; completion begins at state 2.
					max_state = tier + 2;
					max_progress = progress == std::numeric_limits<int>::min()
						? std::numeric_limits<int>::max()
						: std::abs(progress);
				}

				if (max_state == 0)
				{
					continue;
				}

				const auto state_set = set_ranked_stat({"challengeState", challenge}, max_state);
				const auto progress_set = set_ranked_stat({"challengeProgress", challenge}, max_progress);
				if (state_set && progress_set)
				{
					++updated;
				}
			}

			return {updated, total};
		}

		void set_player_data_int(const command::params& params)
		{
			if (params.size() < 3)
			{
				console::info("Usage: setPlayerDataInt <path...> <value>\n");
				return;
			}

			int value{};
			if (!parse_integer(params[params.size() - 1], value))
			{
				console::error("setPlayerDataInt: '%s' is not a valid integer.\n",
					params[params.size() - 1]);
				return;
			}

			if (!has_stats())
			{
				console::error("setPlayerDataInt: player stats are not available.\n");
				return;
			}

			if (params.size() - 2 > static_cast<int>(max_stat_path_elements))
			{
				console::error("setPlayerDataInt: paths are limited to 16 elements.\n");
				return;
			}

			std::vector<unsigned int> path{};
			path.reserve(params.size() - 2);

			for (auto index = 1; index < params.size() - 1; ++index)
			{
				path.emplace_back(game::DDL_HashString(params[index]));
			}

			if (!set_ranked_stat(path.data(), path.size(), value))
			{
				console::error("setPlayerDataInt: invalid ranked stat path or value.\n");
				return;
			}

			console::info("setPlayerDataInt: stat updated.\n");
		}

		void unlock_multiplayer_stats()
		{
			if (!game::environment::is_multiplayer())
			{
				console::error("unlockstatsmp: this command is only available in Multiplayer.\n");
				return;
			}

			if (!has_stats())
			{
				console::error("unlockstatsmp: player stats are not available.\n");
				return;
			}

			bool rank_unlocked{};
			int max_prestige{};
			int max_experience{};
			if (get_rank_caps("mp/rankTable.csv", max_prestige, max_experience))
			{
				const auto prestige_set = set_ranked_stat({"prestige"}, max_prestige);
				const auto experience_set = set_ranked_stat({"experience"}, max_experience);
				rank_unlocked = prestige_set && experience_set;
			}

			const auto weapons = get_weapon_progress();
			std::size_t unlocked_weapons{};
			std::size_t prestiged_weapons{};

			for (const auto& [weapon, progress] : weapons)
			{
				const auto prestige_set = set_weapon_stat(weapon,
					{"prestige", "weaponPrestige", "prestigeLevel"}, max_weapon_prestige);
				const auto level_set = set_weapon_stat(weapon,
					{"level", "weaponRank", "weaponLevel", "rank"}, progress.level);
				const auto experience_set = set_weapon_stat(weapon, {"experience", "xp"},
					progress.experience);

				if (prestige_set)
				{
					++prestiged_weapons;
				}

				if (level_set && experience_set)
				{
					++unlocked_weapons;
				}
			}

			constexpr std::array divisions
			{
				"infantry", "airborne", "armored", "mountain", "expeditionary", "resistance",
				"cavalry", "grenadier", "commando", "scout", "artillery"
			};

			const auto division_progress = get_division_progress();
			std::size_t unlocked_divisions{};
			std::size_t prestiged_divisions{};
			if (division_progress.level > 0)
			{
				for (const auto division : divisions)
				{
					const auto prestige_set = set_ranked_stat_with_leaf({"divisionStats", division},
						{"prestigeLevel", "prestige"}, max_division_prestige);

					const auto level_set = set_ranked_stat_with_leaf({"divisionStats", division},
						{"level", "divisionLevel"}, division_progress.level);
					const auto experience_set = set_ranked_stat_with_leaf({"divisionStats", division},
						{"experience", "xp", "divisionXP"}, division_progress.experience);

					if (prestige_set)
					{
						++prestiged_divisions;
					}

					if (level_set && experience_set)
					{
						++unlocked_divisions;
					}
				}
			}

			const auto [unlocked_challenges, challenges] = unlock_challenges();

			console::debug("unlockstatsmp: rank progression updated: %s.\n",
				rank_unlocked ? "yes" : "no");
			console::debug("unlockstatsmp: %zu of %zu weapon progression entries updated.\n",
				unlocked_weapons, weapons.size());
			console::debug("unlockstatsmp: %zu weapon prestige entries updated.\n",
				prestiged_weapons);
			console::debug("unlockstatsmp: %zu division progression entries updated.\n",
				unlocked_divisions);
			console::debug("unlockstatsmp: %zu division prestige entries updated.\n",
				prestiged_divisions);
			console::debug("unlockstatsmp: %zu of %zu challenge entries updated.\n",
				unlocked_challenges, challenges);

			if (!rank_unlocked)
			{
				console::warn("unlockstatsmp: failed to update rank progression.\n");
			}

			if (weapons.empty())
			{
				console::warn("unlockstatsmp: no weapon-leveling table was available.\n");
			}
			else if (unlocked_weapons == 0)
			{
				console::warn("unlockstatsmp: no compatible weapon progression entries were found.\n");
			}

			if (!weapons.empty() && prestiged_weapons == 0)
			{
				console::warn("unlockstatsmp: no compatible weapon prestige entries were found.\n");
			}

			if (division_progress.level <= 0)
			{
				console::warn("unlockstatsmp: no division-leveling table was available.\n");
			}
			else if (unlocked_divisions == 0)
			{
				console::warn("unlockstatsmp: no compatible division progression entries were found.\n");
			}

			if (division_progress.level > 0 && prestiged_divisions == 0)
			{
				console::warn("unlockstatsmp: no compatible division prestige entries were found.\n");
			}

			if (challenges == 0)
			{
				console::warn("unlockstatsmp: no challenge table was available.\n");
			}
			else if (unlocked_challenges == 0)
			{
				console::warn("unlockstatsmp: no compatible challenge entries were found.\n");
			}

			if (rank_unlocked && unlocked_weapons > 0 && prestiged_weapons > 0 &&
				unlocked_divisions > 0 && prestiged_divisions > 0 && unlocked_challenges > 0)
			{
				console::info("unlockstatsmp: Multiplayer progression and challenges unlocked.\n");
			}
		}

		void unlock_zombie_stats()
		{
			if (!game::environment::is_zombies())
			{
				console::error("unlockstatszm: this command is only available in Zombies.\n");
				return;
			}

			bool rank_unlocked{};
			if (has_stats())
			{
				int max_prestige{};
				int max_experience{};
				unsigned int stats_group{};
				if (get_rank_caps("mp/cp_rankTable.csv", max_prestige, max_experience) &&
					find_zombie_stats_group(stats_group))
				{
					const auto prestige_set = set_stat({"prestigeLevel"}, max_prestige, stats_group);
					const auto experience_set = set_stat({"totalXP"}, max_experience, stats_group);
					rank_unlocked = prestige_set && experience_set;
				}
			}

			console::debug("unlockstatszm: rank progression updated: %s.\n",
				rank_unlocked ? "yes" : "no");
			if (!rank_unlocked)
			{
				console::warn("unlockstatszm: failed to update Zombies rank progression.\n");
			}

			const auto challenges = unlock_zombies::unlock_hidden_challenges();
			console::debug("unlockstatszm: %d of %d Zombies achievement entries completed.\n",
				challenges.completed, challenges.total);
			const auto challenges_unlocked = challenges.persisted && challenges.total > 0 &&
				challenges.completed == challenges.total;
			if (!challenges.persisted)
			{
				console::warn("unlockstatszm: failed to persist Zombies Hidden Challenge progression.\n");
			}
			else if (!challenges_unlocked)
			{
				console::warn("unlockstatszm: some Zombies Hidden Challenge definitions could not be resolved.\n");
			}

			if (rank_unlocked && challenges_unlocked)
			{
				console::info("unlockstatszm: Zombies progression and Hidden Challenges unlocked.\n");
			}
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated())
			{
				return;
			}

			command::add("setPlayerDataInt", set_player_data_int);
			command::add("unlockstatsmp", unlock_multiplayer_stats);
			command::add("unlockstatszm", unlock_zombie_stats);
		}
	};
}

REGISTER_COMPONENT(stats::component)
