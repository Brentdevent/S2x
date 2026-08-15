#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "unlock_zombies.hpp"

#include "component/scheduler.hpp"

#include "game/game.hpp"
#include "game/demonware/achievement_store.hpp"

#include <utils/hook.hpp>

#include <charconv>
#include <mutex>
#include <optional>
#include <vector>

namespace unlock_zombies
{
	namespace
	{
		constexpr int unlimited_consumable_quantity = 999;
		constexpr int zombie_weapon_challenges_achievement_id = 1141;
		constexpr std::uint16_t zombie_weapon_challenges_completed_progress = 31;
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
		constexpr std::ptrdiff_t achievement_response_offset = 0xF8;

		const game::dvar_t* cg_unlock_all_zm_challenges{};
		const game::dvar_t* cg_unlock_all_zm_consumables{};

		utils::hook::detour get_item_quantity_hook;
		utils::hook::detour fetch_user_achievements_hook;
		std::atomic_uint64_t achievement_refresh_generation{};
		std::mutex pending_achievement_update_mutex{};
		std::optional<std::string> pending_achievement_update{};

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

		const char* find_achievement_name(const game::StringTable* definitions, const int id)
		{
			const auto id_string = std::to_string(id);
			for (auto row = 0; definitions && row < definitions->rowCount; ++row)
			{
				const auto* value = get_cell(definitions, row, achievement_definition_id_column);
				if (value && id_string == value)
				{
					return get_cell(definitions, row, achievement_definition_name_column);
				}
			}

			return nullptr;
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
					!get_category_progress(challenges, row, progress) ||
					std::find_if(values.begin(), values.end(), [id](const auto& value)
					{
						return value.first == id;
					}) != values.end())
				{
					continue;
				}

				values.emplace_back(id, progress);
			}

			// The four challenge-locked starting weapons share this five-bit achievement.
			values.emplace_back(zombie_weapon_challenges_achievement_id,
				zombie_weapon_challenges_completed_progress);
			values.insert(values.end(), supplemental_zombie_achievements.begin(),
				supplemental_zombie_achievements.end());

			zombie_achievement_list result{};
			result.total = static_cast<int>(values.size());
			result.records.reserve(values.size());
			for (const auto& [id, progress] : values)
			{
				const auto* name = find_achievement_name(definitions, id);
				if (!name || !*name)
				{
					continue;
				}

				demonware::achievement_record achievement{};
				achievement.name = name;
				achievement.progress = progress;
				achievement.progress_target = progress;
				achievement.fulfilled_times = 1;
				achievement.status = demonware::achievement_status::finished;
				result.records.push_back(std::move(achievement));
			}

			return result;
		}

		int get_item_quantity_stub(const unsigned int controller_index, const unsigned int item_guid)
		{
			if (cg_unlock_all_zm_consumables && cg_unlock_all_zm_consumables->current.enabled &&
				game::Inventory_IsItemGuidAZMConsumable(item_guid))
			{
				return unlimited_consumable_quantity;
			}

			return get_item_quantity_hook.invoke<int>(controller_index, item_guid);
		}

		void clear_pending_achievement_update()
		{
			std::lock_guard lock{pending_achievement_update_mutex};
			pending_achievement_update.reset();
		}

		void queue_achievement_update(std::string transaction)
		{
			std::lock_guard lock{pending_achievement_update_mutex};
			pending_achievement_update = std::move(transaction);
		}

		std::optional<std::string> take_achievement_update()
		{
			std::lock_guard lock{pending_achievement_update_mutex};
			auto transaction = std::move(pending_achievement_update);
			pending_achievement_update.reset();
			return transaction;
		}

		std::string make_user_achievement_response(const std::string_view client_transaction)
		{
			const auto records = demonware::achievement_store::get_all();
			rapidjson::Document document{};
			document.SetObject();
			auto& allocator = document.GetAllocator();
			document.AddMember("Action", "get_user_achievements", allocator);
			document.AddMember("Status", "ok", allocator);
			document.AddMember("ClientTx", rapidjson::Value{client_transaction.data(),
				static_cast<rapidjson::SizeType>(client_transaction.size()), allocator}, allocator);

			rapidjson::Value achievements{rapidjson::kArrayType};
			for (const auto& record : records)
			{
				rapidjson::Value value{rapidjson::kObjectType};
				value.AddMember("kind", record.kind, allocator);
				value.AddMember("name", rapidjson::Value{record.name.data(),
					static_cast<rapidjson::SizeType>(record.name.size()), allocator}, allocator);
				value.AddMember("requiresClaim", false, allocator);
				value.AddMember("progress", record.progress, allocator);
				value.AddMember("progressTarget", record.progress_target, allocator);
				value.AddMember("fulfilledTimes", record.fulfilled_times, allocator);
				value.AddMember("completionTimestamp", record.completion_timestamp, allocator);
				value.AddMember("status", rapidjson::Value{
					demonware::get_achievement_status_name(record.status), allocator}, allocator);
				achievements.PushBack(value, allocator);
			}

			document.AddMember("Achievements", achievements, allocator);
			document.AddMember("NextPageToken", "", allocator);

			rapidjson::StringBuffer buffer{};
			rapidjson::Writer<rapidjson::StringBuffer, rapidjson::Document::EncodingType,
				rapidjson::ASCII<>> writer{buffer};
			document.Accept(writer);
			return {buffer.GetString(), buffer.GetSize()};
		}

		bool fetch_user_achievements_stub(const unsigned int controller_index, const void* page_token,
			const void* transaction_id, const unsigned int account_index)
		{
			const auto result = fetch_user_achievements_hook.invoke<bool>(controller_index,
				page_token, transaction_id, account_index);
			if (!cg_unlock_all_zm_challenges ||
				!cg_unlock_all_zm_challenges->current.enabled)
			{
				clear_pending_achievement_update();
				return result;
			}

			if (result && transaction_id && controller_index == 0)
			{
				queue_achievement_update(static_cast<const char*>(transaction_id));
			}

			return result;
		}

		void dispatch_user_achievement_updates()
		{
			if (!cg_unlock_all_zm_challenges ||
				!cg_unlock_all_zm_challenges->current.enabled)
			{
				clear_pending_achievement_update();
				return;
			}

			if (const auto transaction = take_achievement_update())
			{
				const auto response = make_user_achievement_response(*transaction);
				auto* response_object = game::AE_UserAchievementTaskData.get() +
					achievement_response_offset;
				if (game::AE_SetResponseString(response_object, response.c_str()))
				{
					game::AE_ProcessResponse(0, response_object, 0);
				}
			}
		}

		bool refresh_user_achievements()
		{
			std::array<char, 32> transaction_id{};
			game::AE_GenerateTransactionId(transaction_id.data());
			return game::AE_FetchUserAchievementsByPage(0, "",
				transaction_id.data(), 0);
		}

		void refresh_user_achievements_when_ready()
		{
			const auto generation = ++achievement_refresh_generation;
			if (refresh_user_achievements())
			{
				return;
			}

			scheduler::schedule([generation, attempts = 0]() mutable
			{
				if (achievement_refresh_generation.load() != generation ||
					refresh_user_achievements())
				{
					return scheduler::cond_end;
				}

				return ++attempts >= 30 ? scheduler::cond_end : scheduler::cond_continue;
			}, scheduler::pipeline::main, 1s);
		}

	}

	challenge_unlock_result enable_challenges()
	{
		if (!cg_unlock_all_zm_challenges)
		{
			return {};
		}

		game::Dvar_SetBool(const_cast<game::dvar_t*>(cg_unlock_all_zm_challenges), true);
		challenge_unlock_result result{};
		result.enabled = cg_unlock_all_zm_challenges->current.enabled;
		if (result.enabled)
		{
			const auto achievements = get_zombie_challenge_achievements();
			result.total = achievements.total;
			if (!achievements.records.empty() &&
				demonware::achievement_store::merge(achievements.records))
			{
				result.completed = static_cast<int>(achievements.records.size());
				refresh_user_achievements_when_ready();
			}
		}

		return result;
	}

	bool enable_consumables()
	{
		if (!cg_unlock_all_zm_consumables)
		{
			return false;
		}

		game::Dvar_SetBool(const_cast<game::dvar_t*>(cg_unlock_all_zm_consumables), true);
		return cg_unlock_all_zm_consumables->current.enabled;
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

			cg_unlock_all_zm_challenges = game::Dvar_RegisterBool(
				"cg_unlockall_zm_challenges", false, game::DVAR_FLAG_SAVED);
			cg_unlock_all_zm_consumables = game::Dvar_RegisterBool(
				"cg_unlockall_zm_consumables", false, game::DVAR_FLAG_SAVED);

			get_item_quantity_hook.create(game::Inventory_GetItemQuantity, get_item_quantity_stub);
			fetch_user_achievements_hook.create(game::AE_FetchUserAchievementsByPage,
				fetch_user_achievements_stub);
			scheduler::loop(dispatch_user_achievement_updates, scheduler::pipeline::main, 50ms);

			scheduler::once([]
			{
				if (cg_unlock_all_zm_challenges && cg_unlock_all_zm_challenges->current.enabled)
				{
					refresh_user_achievements_when_ready();
				}
			}, scheduler::pipeline::main, 5s);
		}
	};
}

REGISTER_COMPONENT(unlock_zombies::component)
