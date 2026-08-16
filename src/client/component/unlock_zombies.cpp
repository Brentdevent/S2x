#include <std_include.hpp>
#include "unlock_zombies.hpp"

#include "component/achievement_sync.hpp"

#include "game/game.hpp"
#include "game/demonware/achievement_store.hpp"

#include <charconv>
#include <unordered_set>
#include <vector>

namespace unlock_zombies
{
	namespace
	{
		constexpr std::array supplemental_zombie_achievements
		{
			std::pair{1112, std::uint16_t{1}}, // Tortured Path maps completed.
			std::pair{1114, std::uint16_t{1}}, // DLC3 survival maps unlocked.
			std::pair{1142, std::uint16_t{1}}, // Zombies master-prestige reward.
			std::pair{1143, std::uint16_t{1}}, // All Zombies challenge sets completed.
			std::pair{1144, std::uint16_t{1}}, // Zombies master-prestige reward 2.
			std::pair{1145, std::uint16_t{1}}, // Zombies master-prestige reward 3 (Rookbane).
		};
		constexpr int achievement_id_column = 5;
		constexpr int challenge_reference_column = 0;
		constexpr int category_challenges_column = 4;
		constexpr int challenge_bit_column = 5;
		constexpr int achievement_definition_id_column = 0;
		constexpr int achievement_definition_name_column = 1;
		constexpr int achievement_definition_kind_column = 2;

		struct zombie_achievement_list
		{
			std::vector<demonware::achievement_record> records{};
			int total{};
		};

		const char* get_cell(const game::StringTable* table, const int row, const int column)
		{
			if (!table || !table->values || row < 0 || row >= table->rowCount || column < 0 ||
				column >= table->columnCount)
			{
				return nullptr;
			}

			return table->values[row * table->columnCount + column].string;
		}

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

		void append_unique_achievement(std::vector<std::pair<int, std::uint16_t>>& values,
			const int id, const std::uint16_t progress)
		{
			const auto existing = std::find_if(values.begin(), values.end(), [id](const auto& value)
			{
				return value.first == id;
			});
			if (existing == values.end())
			{
				values.emplace_back(id, progress);
			}
		}

		int find_row_by_reference(const game::StringTable* table, const std::string_view reference)
		{
			for (auto row = 0; table && row < table->rowCount; ++row)
			{
				const auto* value = get_cell(table, row, challenge_reference_column);
				if (value && reference == value)
				{
					return row;
				}
			}

			return -1;
		}

		bool find_achievement_definition(const game::StringTable* definitions, const int id,
			const char*& name, int& kind)
		{
			const auto id_string = std::to_string(id);
			for (auto row = 0; definitions && row < definitions->rowCount; ++row)
			{
				const auto* value = get_cell(definitions, row, achievement_definition_id_column);
				if (value && id_string == value)
				{
					name = get_cell(definitions, row, achievement_definition_name_column);
					return name && *name &&
						parse_integer(get_cell(definitions, row, achievement_definition_kind_column), kind) &&
						kind > 0;
				}
			}

			return false;
		}

		bool get_category_progress(const game::StringTable* challenges, const int category_row,
			std::uint16_t& progress)
		{
			const auto* challenge_list = get_cell(challenges, category_row, category_challenges_column);
			if (!challenge_list || !*challenge_list)
			{
				return false;
			}

			std::uint32_t mask{};
			std::string_view references{challenge_list};
			while (!references.empty())
			{
				const auto separator = references.find(',');
				auto reference = references.substr(0, separator);
				const auto first = reference.find_first_not_of(" \t");
				const auto last = reference.find_last_not_of(" \t");
				if (first != std::string_view::npos)
				{
					reference = reference.substr(first, last - first + 1);
					const auto challenge_row = find_row_by_reference(challenges, reference);
					int bit{};
					if (challenge_row < 0 ||
						!parse_integer(get_cell(challenges, challenge_row, challenge_bit_column), bit) ||
						bit <= 0 || bit > std::numeric_limits<std::uint16_t>::digits)
					{
						return false;
					}

					// Challenge positions in zombieCostumeChallenges.csv are one-based.
					mask |= 1u << (bit - 1);
				}

				if (separator == std::string_view::npos)
				{
					break;
				}

				references.remove_prefix(separator + 1);
			}

			if (!mask)
			{
				return false;
			}

			progress = static_cast<std::uint16_t>(mask);
			return true;
		}

		zombie_achievement_list get_zombie_challenge_achievements()
		{
			std::vector<std::pair<int, std::uint16_t>> values{};
			const auto* challenges = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,
				"mp/zombieCostumeChallenges.csv", false).stringTable;
			const auto* definitions = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,
				"dw/dwGameChallenges.csv", false).stringTable;

			for (auto row = 0; challenges && row < challenges->rowCount; ++row)
			{
				const auto* reference = get_cell(challenges, row, challenge_reference_column);
				if (!reference || !std::string_view{reference}.starts_with("category"))
				{
					continue;
				}

				int id{};
				std::uint16_t progress{};
				if (!parse_integer(get_cell(challenges, row, achievement_id_column), id) || id <= 0 ||
					!get_category_progress(challenges, row, progress))
				{
					continue;
				}

				append_unique_achievement(values, id, progress);
			}

			for (const auto& [id, progress] : supplemental_zombie_achievements)
			{
				append_unique_achievement(values, id, progress);
			}

			zombie_achievement_list result{};
			result.total = static_cast<int>(values.size());
			result.records.reserve(values.size());
			std::unordered_set<std::string> achievement_names{};
			for (const auto& [id, progress] : values)
			{
				const char* name{};
				int kind{};
				if (!find_achievement_definition(definitions, id, name, kind))
				{
					continue;
				}
				if (!achievement_names.emplace(name).second)
				{
					continue;
				}

				demonware::achievement_record achievement{};
				achievement.name = name;
				achievement.kind = kind;
				achievement.progress = progress;
				achievement.progress_target = progress;
				achievement.fulfilled_times = 1;
				achievement.status = demonware::achievement_status::finished;
				result.records.push_back(std::move(achievement));
			}

			return result;
		}

	}

	hidden_challenge_unlock_result unlock_hidden_challenges()
	{
		hidden_challenge_unlock_result result{};
		const auto achievements = get_zombie_challenge_achievements();
		result.total = achievements.total;
		if (!achievements.records.empty() &&
			demonware::achievement_store::merge(achievements.records))
		{
			result.persisted = true;
			result.completed = static_cast<int>(achievements.records.size());
			achievement_sync::request_refresh();
		}

		return result;
	}

}
