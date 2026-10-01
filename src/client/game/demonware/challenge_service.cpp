#include <std_include.hpp>

#include "challenge_service.hpp"
#include "achievement_response.hpp"
#include "achievement_store.hpp"
#include "loot_store.hpp"

#include "component/console/console.hpp"

#include "game/game.hpp"
#include "game/string_table.hpp"

#include <mutex>
#include <random>

namespace demonware::challenge_service
{
	namespace
	{
		constexpr auto challenges_table = "dw/dwGameChallenges.csv";
		constexpr auto events_table = "dw/dwGameEvents.csv";
		constexpr auto periodic_table = "mp/periodicChallengeTable.csv";

		constexpr auto periodic_difficulty_column = 6;
		constexpr auto periodic_target_column = 8;
		constexpr auto periodic_time_column = 10;
		constexpr auto periodic_cost_column = 11;

		constexpr std::uint64_t day_seconds = 24 * 60 * 60;
		constexpr std::uint64_t contract_lifetime = 7 * day_seconds;
		constexpr std::uint32_t contract_sku_base = 3000;

		constexpr std::uint32_t currency_armory_credits = 6;
		constexpr std::uint32_t daily_reward_credits = 50;
		constexpr std::uint32_t supply_drop = 1;
		constexpr std::uint32_t rare_supply_drop = 2;

		constexpr auto above_beyond_daily = "above_beyond_daily";
		constexpr auto above_beyond_weekly = "above_beyond_weekly";
		constexpr std::uint16_t above_beyond_daily_target = 6;
		constexpr std::uint16_t above_beyond_weekly_target = 3;

		enum challenge_kind : int
		{
			kind_daily = 1,
			kind_weekly = 2,
			kind_special = 3,
			kind_contract = 4,
			kind_always_on = 5,
		};

		struct kind_config
		{
			int kind;
			std::size_t offers;
			int activation_limit;
		};

		constexpr kind_config kind_configs[]
		{
			{kind_daily, 3, 3},
			{kind_weekly, 3, 3},
			{kind_special, 1, 1},
			{kind_contract, 3, 3},
		};

		struct condition
		{
			std::string selector{};
			std::uint64_t value{};
		};

		struct challenge
		{
			std::uint32_t id{};
			std::string name{};
			int kind{};
			std::uint32_t event_id{};
			std::vector<condition> conditions{};
			std::uint16_t target{};
			std::uint32_t usage_time{};
			std::uint32_t cost_item{};
			std::uint32_t price{};
		};

		struct catalog
		{
			std::vector<challenge> challenges{};
			std::map<std::string, std::uint32_t, std::less<>> events{};
		};

		std::mutex catalog_mutex{};
		std::optional<catalog> loaded_catalog{};

		using allocator_type = rapidjson::Document::AllocatorType;

		bool parse_uint(const char* value, std::uint64_t& result, const int base = 10)
		{
			if (!value || !*value)
			{
				return false;
			}

			char* end{};
			result = std::strtoull(value, &end, base);
			return end && !*end;
		}

		std::vector<condition> parse_conditions(const std::string_view text)
		{
			std::vector<condition> result{};
			std::size_t offset{};
			while (offset < text.size())
			{
				const auto open = text.find('(', offset);
				const auto close = text.find(')', open);
				if (open == std::string_view::npos || close == std::string_view::npos)
				{
					break;
				}

				const auto inner = text.substr(open + 1, close - open - 1);
				const auto separator = inner.find(':');
				std::uint64_t value{};
				if (separator != std::string_view::npos &&
					parse_uint(std::string{inner.substr(separator + 1)}.data(), value))
				{
					result.push_back({std::string{inner.substr(0, separator)}, value});
				}

				offset = close + 1;
			}

			return result;
		}

		std::uint32_t get_price(const char* difficulty)
		{
			const std::string_view value{difficulty ? difficulty : ""};
			if (value == "AEC_EASY")
				return 100;
			else if (value == "AEC_HARD")
				return 300;
			return 200;
		}

		std::uint64_t get_period(const int kind, const std::uint64_t now)
		{
			const auto day = now / day_seconds;
			return kind == kind_daily || kind == kind_contract ? day : (day + 3) / 7;
		}

		std::uint64_t get_period_end(const int kind, const std::uint64_t now)
		{
			const auto period = get_period(kind, now) + 1;
			return kind == kind_daily || kind == kind_contract ? period * day_seconds : (period * 7 - 3) * day_seconds;
		}

		std::vector<const challenge*> get_offers(const catalog& data, const std::uint64_t now)
		{
			std::vector<const challenge*> offers{};
			for (const auto& config : kind_configs)
			{
				std::vector<const challenge*> candidates{};
				for (const auto& entry : data.challenges)
				{
					if (entry.kind == config.kind)
					{
						candidates.push_back(&entry);
					}
				}

				std::mt19937_64 engine{get_period(config.kind, now) * 31 + config.kind};
				std::shuffle(candidates.begin(), candidates.end(), engine);
				candidates.resize(std::min(candidates.size(), config.offers));
				offers.insert(offers.end(), candidates.begin(), candidates.end());
			}
			return offers;
		}

		const challenge* find_offer(const catalog& data, const std::string_view name, const std::uint64_t now)
		{
			for (const auto* entry : get_offers(data, now))
			{
				if (entry->name == name)
				{
					return entry;
				}
			}

			return nullptr;
		}

		const challenge* find_challenge(const catalog& data, const std::string_view name)
		{
			for (const auto& entry : data.challenges)
			{
				if (entry.name == name)
				{
					return &entry;
				}
			}

			return nullptr;
		}

		int get_activation_limit(const int kind)
		{
			for (const auto& config : kind_configs)
			{
				if (config.kind == kind)
				{
					return config.activation_limit;
				}
			}

			return 0;
		}

		bool is_active(const achievement_record& record, const std::uint64_t now)
		{
			return (record.status == achievement_status::in_progress || record.status == achievement_status::claimable)
				&& (!record.expiration_timestamp || record.expiration_timestamp > now);
		}

		rapidjson::Value make_string(const std::string_view value, allocator_type& allocator)
		{
			return rapidjson::Value{value.data(), static_cast<rapidjson::SizeType>(value.size()), allocator};
		}

		std::string serialize(const rapidjson::Document& document)
		{
			rapidjson::StringBuffer buffer{};
			rapidjson::Writer<rapidjson::StringBuffer, rapidjson::Document::EncodingType,
				rapidjson::ASCII<>> writer{buffer};
			document.Accept(writer);
			return {buffer.GetString(), buffer.GetSize()};
		}

		rapidjson::Document make_response(const std::string_view action, const std::string_view client_transaction,
			const bool success)
		{
			rapidjson::Document response{};
			response.SetObject();
			auto& allocator = response.GetAllocator();
			response.AddMember("Version", 0, allocator);
			response.AddMember("Action", make_string(action, allocator), allocator);
			response.AddMember("Status", rapidjson::StringRef(success ? "ok" : "error"), allocator);
			response.AddMember("ClientTx", make_string(client_transaction, allocator), allocator);
			return response;
		}

		std::string make_push(const achievement_record& record, const char* reason, const char* type,
			rapidjson::Value triggers, rapidjson::Document& document)
		{
			document.SetObject();
			auto& allocator = document.GetAllocator();
			document.AddMember("name", make_string(record.name, allocator), allocator);
			document.AddMember("kind", record.kind, allocator);
			document.AddMember("requiresClaim", record.requires_claim, allocator);
			document.AddMember("progress", record.progress, allocator);
			document.AddMember("progressTarget", record.progress_target, allocator);
			document.AddMember("fulfilledTimes", record.fulfilled_times, allocator);
			document.AddMember("completionTimestamp", record.completion_timestamp, allocator);
			document.AddMember("status", rapidjson::StringRef(get_achievement_status_name(record.status)), allocator);
			document.AddMember("reason", rapidjson::StringRef(reason), allocator);
			document.AddMember("type", rapidjson::StringRef(type), allocator);
			document.AddMember("triggers", triggers, allocator);
			return serialize(document);
		}

		std::string make_push(const achievement_record& record, const char* reason, const char* type)
		{
			rapidjson::Document document{};
			return make_push(record, reason, type, rapidjson::Value{rapidjson::kArrayType}, document);
		}

		std::optional<std::string> grant_reward(const achievement_record& record, const char* type)
		{
			const auto now = static_cast<std::uint64_t>(time(nullptr));
			std::uint32_t item_quantity{};
			std::uint32_t balance_before{};
			const auto saved = loot_store::mutate([&](loot_store::state& state)
			{
				if (record.reward_item)
				{
					item_quantity = ++state.items[record.reward_item];
				}

				if (record.reward_currency && record.reward_amount)
				{
					auto& balance = state.currencies[record.reward_currency];
					balance_before = balance;
					balance += record.reward_amount;
				}

				return true;
			});

			if (!saved)
			{
				return std::nullopt;
			}

			rapidjson::Document document{};
			auto& allocator = document.GetAllocator();
			rapidjson::Value triggers{rapidjson::kArrayType};
			if (record.reward_item)
			{
				rapidjson::Value entry{rapidjson::kObjectType};
				entry.AddMember("item_id", record.reward_item, allocator);
				entry.AddMember("collision_field", 0, allocator);
				entry.AddMember("expiry_duration", 0, allocator);
				entry.AddMember("item_quantity", item_quantity, allocator);
				entry.AddMember("mod_date_time", static_cast<std::uint32_t>(now), allocator);

				rapidjson::Value inventory_items{rapidjson::kArrayType};
				inventory_items.PushBack(entry, allocator);

				rapidjson::Value inventory{rapidjson::kObjectType};
				inventory.AddMember("detailed_inventory", inventory_items, allocator);
				inventory.AddMember("currencies", rapidjson::Value{rapidjson::kArrayType}, allocator);

				rapidjson::Value trigger{rapidjson::kObjectType};
				trigger.AddMember("type", "GRANT_PRODUCT", allocator);
				trigger.AddMember("inventory", inventory, allocator);
				triggers.PushBack(trigger, allocator);
			}

			if (record.reward_currency && record.reward_amount)
			{
				rapidjson::Value currency{rapidjson::kObjectType};
				currency.AddMember("currency_id", record.reward_currency, allocator);
				currency.AddMember("balance_delta", record.reward_amount, allocator);
				currency.AddMember("balance_before", balance_before, allocator);

				rapidjson::Value currencies{rapidjson::kArrayType};
				currencies.PushBack(currency, allocator);

				rapidjson::Value inventory{rapidjson::kObjectType};
				inventory.AddMember("currencies", currencies, allocator);

				rapidjson::Value trigger{rapidjson::kObjectType};
				trigger.AddMember("type", "GRANT_CURRENCY", allocator);
				trigger.AddMember("inventory", inventory, allocator);
				triggers.PushBack(trigger, allocator);
			}

			return make_push(record, "completed", type, std::move(triggers), document);
		}

		void advance_above_beyond(const int kind, std::vector<std::string>& pushes)
		{
			const auto daily = kind == kind_daily;
			if (!daily && kind != kind_weekly)
			{
				return;
			}

			const auto target = daily ? above_beyond_daily_target : above_beyond_weekly_target;
			const auto now = static_cast<std::uint64_t>(time(nullptr));
			achievement_record result{};
			auto completed = false;
			const auto status = achievement_store::mutate(daily ? above_beyond_daily : above_beyond_weekly,
				[&](achievement_record& record)
				{
					if (record.kind != kind_always_on)
					{
						record.fulfilled_times = 0;
						record.progress = 0;
					}

					record.kind = kind_always_on;
					record.requires_claim = false;
					record.progress_target = target;
					record.reward_item = daily ? supply_drop : rare_supply_drop;
					if (record.status != achievement_status::in_progress || record.progress >= target)
					{
						record.progress = 0;
					}

					++record.progress;
					completed = record.progress >= target;
					record.status = completed ? achievement_status::finished : achievement_status::in_progress;
					if (completed)
					{
						++record.fulfilled_times;
						record.completion_timestamp = now;
					}

					result = record;
					if (completed)
					{
						record.progress = 0;
						record.status = achievement_status::in_progress;
					}

					return true;
				});

			if (status != achievement_store::mutation_result::updated)
			{
				return;
			}

			if (!completed)
			{
				pushes.push_back(make_push(result, "inProgress", "ACHIEVEMENT"));
				return;
			}

			if (const auto push = grant_reward(result, "ACHIEVEMENT"))
			{
				pushes.push_back(*push);
				console::demonware("[DW] challenge: %s completed\n", result.name.data());
			}
		}

		action_result get_scheduled(const catalog& data, const std::string_view action,
			const std::string_view client_transaction)
		{
			const auto now = static_cast<std::uint64_t>(time(nullptr));
			std::map<std::string, achievement_record, std::less<>> records{};
			for (auto& record : achievement_store::get_all())
			{
				records[record.name] = std::move(record);
			}

			auto response = make_response(action, client_transaction, true);
			auto& allocator = response.GetAllocator();

			rapidjson::Value next_periods{rapidjson::kObjectType};
			rapidjson::Value limits{rapidjson::kObjectType};
			for (const auto& config : kind_configs)
			{
				const auto key = std::to_string(config.kind);
				rapidjson::Value period_key = make_string(key, allocator);
				rapidjson::Value period_value{get_period_end(config.kind, now)};
				next_periods.AddMember(period_key, period_value, allocator);

				rapidjson::Value limit_key = make_string(key, allocator);
				rapidjson::Value limit_value{config.activation_limit};
				limits.AddMember(limit_key, limit_value, allocator);
			}

			rapidjson::Value achievements{rapidjson::kArrayType};
			for (const auto* entry : get_offers(data, now))
			{
				const char* status = "available";
				const auto record = records.find(entry->name);
				if (record != records.end() && record->second.activation_timestamp &&
					get_period(entry->kind, record->second.activation_timestamp) == get_period(entry->kind, now))
				{
					switch (record->second.status)
					{
					case achievement_status::in_progress:
						status = "in_progress";
						break;
					case achievement_status::claimable:
						status = "claimable";
						break;
					case achievement_status::finished:
						status = "completed";
						break;
					default:
						break;
					}
				}

				achievement_record offer{};
				offer.reward_item = entry->kind == kind_weekly || entry->kind == kind_contract
					? supply_drop : entry->kind == kind_special ? rare_supply_drop : 0;
				offer.reward_currency = entry->kind == kind_daily ? currency_armory_credits : 0;
				offer.reward_amount = entry->kind == kind_daily ? daily_reward_credits : 0;

				rapidjson::Value value{rapidjson::kObjectType};
				value.AddMember("kind", entry->kind, allocator);
				value.AddMember("name", make_string(entry->name, allocator), allocator);
				value.AddMember("progressTarget", entry->target, allocator);
				value.AddMember("requiresClaim", true, allocator);
				value.AddMember("status", rapidjson::StringRef(status), allocator);
				value.AddMember("expirationTimestamp", entry->kind == kind_contract
					? now + contract_lifetime : get_period_end(entry->kind, now), allocator);
				if (entry->kind == kind_contract && entry->usage_time)
				{
					value.AddMember("usageTimeTarget", entry->usage_time, allocator);
				}

				value.AddMember("successRewards", achievement_response::serialize_rewards(offer, allocator), allocator);
				achievements.PushBack(value, allocator);
			}

			response.AddMember("NextPeriodStartTimes", next_periods, allocator);
			response.AddMember("ActivationLimits", limits, allocator);
			response.AddMember("Achievements", achievements, allocator);
			return {serialize(response)};
		}

		action_result activate(const catalog& data, const std::string_view action,
			const std::string_view client_transaction, const std::string_view name)
		{
			const auto now = static_cast<std::uint64_t>(time(nullptr));
			const auto* entry = find_offer(data, name, now);
			if (!entry)
			{
				console::demonware("[DW] challenge: '%.*s' is not offered\n", static_cast<int>(name.size()), name.data());
				return {serialize(make_response(action, client_transaction, false))};
			}

			auto active = 0;
			for (const auto& record : achievement_store::get_all())
			{
				if (record.kind == entry->kind && record.name != entry->name && is_active(record, now))
				{
					++active;
				}
			}

			if (active >= get_activation_limit(entry->kind))
			{
				console::demonware("[DW] challenge: activation limit reached for kind %d\n", entry->kind);
				return {serialize(make_response(action, client_transaction, false))};
			}

			const auto result = achievement_store::mutate(entry->name, [&](achievement_record& record)
			{
				if (record.activation_timestamp &&
					get_period(entry->kind, record.activation_timestamp) == get_period(entry->kind, now) &&
					record.status != achievement_status::inactive)
				{
					return false;
				}

				record = {};
				record.kind = entry->kind;
				record.progress_target = entry->target;
				record.fulfilled_times = 0;
				record.status = achievement_status::in_progress;
				record.requires_claim = true;
				record.activation_timestamp = now;
				record.expiration_timestamp = entry->kind == kind_contract
					? now + contract_lifetime : get_period_end(entry->kind, now);
				record.usage_time_target = entry->kind == kind_contract ? entry->usage_time : 0;
				record.reward_item = entry->kind == kind_weekly || entry->kind == kind_contract
					? supply_drop : entry->kind == kind_special ? rare_supply_drop : 0;
				record.reward_currency = entry->kind == kind_daily ? currency_armory_credits : 0;
				record.reward_amount = entry->kind == kind_daily ? daily_reward_credits : 0;
				return true;
			});

			const auto success = result == achievement_store::mutation_result::updated;
			if (success && entry->kind == kind_contract && entry->cost_item)
			{
				loot_store::mutate([&](loot_store::state& state)
				{
					const auto owned = state.items.find(entry->cost_item);
					if (owned == state.items.end() || !owned->second)
					{
						return false;
					}

					if (!--owned->second)
					{
						state.items.erase(owned);
					}

					return true;
				});
			}

			console::demonware("[DW] challenge: activate '%s' %s\n", entry->name.data(), success ? "ok" : "rejected");
			return {serialize(make_response(action, client_transaction, success))};
		}

		action_result deactivate(const std::string_view action, const std::string_view client_transaction,
			const std::string_view name)
		{
			auto success = false;
			for (const auto& record : achievement_store::get_all())
			{
				if (record.name == name && record.kind >= kind_daily && record.kind <= kind_contract)
				{
					success = achievement_store::erase(record.name);
					break;
				}
			}

			console::demonware("[DW] challenge: deactivate '%.*s' %s\n", static_cast<int>(name.size()), name.data(),
				success ? "ok" : "rejected");
			return {serialize(make_response(action, client_transaction, success))};
		}

		action_result claim(const std::string_view action, const std::string_view client_transaction,
			const std::string_view name)
		{
			const auto now = static_cast<std::uint64_t>(time(nullptr));
			achievement_record claimed{};
			const auto status = achievement_store::mutate(std::string{name}, [&](achievement_record& record)
			{
				if (record.status != achievement_status::claimable)
				{
					return false;
				}

				record.status = achievement_status::finished;
				record.fulfilled_times = std::max(record.fulfilled_times, 0) + 1;
				record.completion_timestamp = now;
				claimed = record;
				return true;
			});

			const auto success = status == achievement_store::mutation_result::updated;
			action_result result{serialize(make_response(action, client_transaction, success))};
			if (!success)
			{
				console::demonware("[DW] challenge: claim '%.*s' rejected\n", static_cast<int>(name.size()), name.data());
				return result;
			}

			if (const auto push = grant_reward(claimed, "CHALLENGE"))
			{
				result.pushes.push_back(*push);
			}

			advance_above_beyond(claimed.kind, result.pushes);
			console::demonware("[DW] challenge: claimed '%s'\n", claimed.name.data());
			return result;
		}

		bool matches(const challenge& entry, const reward_game_events::event& event)
		{
			for (const auto& required : entry.conditions)
			{
				const auto found = std::ranges::any_of(event.parameters, [&](const reward_game_events::parameter& value)
				{
					return value.selector == required.selector && value.value == required.value;
				});

				if (!found)
				{
					return false;
				}
			}

			return true;
		}
	}

	bool load()
	{
		{
			std::lock_guard lock{catalog_mutex};
			if (loaded_catalog)
			{
				return true;
			}
		}

		const auto* challenges = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE, challenges_table, false).stringTable;
		const auto* events = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE, events_table, false).stringTable;
		const auto* periodic = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE, periodic_table, false).stringTable;
		if (!challenges || !events || !periodic || challenges->rowCount <= 1 || events->rowCount <= 1 ||
			periodic->rowCount <= 1)
		{
			return false;
		}

		std::map<std::uint64_t, int> periodic_rows{};
		for (auto row = 0; row < periodic->rowCount; ++row)
		{
			std::uint64_t id{};
			if (parse_uint(game::string_table::get_cell(periodic, row, 0), id))
			{
				periodic_rows.emplace(id, row);
			}
		}

		catalog result{};
		for (auto row = 0; row < events->rowCount; ++row)
		{
			std::uint64_t id{};
			const auto* name = game::string_table::get_cell(events, row, 1);
			if (name && *name && parse_uint(game::string_table::get_cell(events, row, 0), id))
			{
				result.events.emplace(name, static_cast<std::uint32_t>(id));
			}
		}

		for (auto row = 0; row < challenges->rowCount; ++row)
		{
			std::uint64_t id{};
			std::uint64_t kind{};
			std::uint64_t event_id{};
			const auto* name = game::string_table::get_cell(challenges, row, 1);
			if (!name || !*name || !parse_uint(game::string_table::get_cell(challenges, row, 0), id) ||
				!parse_uint(game::string_table::get_cell(challenges, row, 2), kind) ||
				!parse_uint(game::string_table::get_cell(challenges, row, 3), event_id) ||
				kind < kind_daily || kind > kind_contract)
			{
				continue;
			}

			const auto periodic_row = periodic_rows.find(id);
			std::uint64_t target{};
			if (periodic_row == periodic_rows.end() ||
				!parse_uint(game::string_table::get_cell(periodic, periodic_row->second, periodic_target_column), target) ||
				!target || target > std::numeric_limits<std::uint16_t>::max())
			{
				continue;
			}

			challenge entry{};
			entry.id = static_cast<std::uint32_t>(id);
			entry.name = name;
			entry.kind = static_cast<int>(kind);
			entry.event_id = static_cast<std::uint32_t>(event_id);
			const auto* conditions = game::string_table::get_cell(challenges, row, 4);
			entry.conditions = parse_conditions(conditions ? conditions : "");
			entry.target = static_cast<std::uint16_t>(target);

			std::uint64_t value{};
			if (parse_uint(game::string_table::get_cell(periodic, periodic_row->second, periodic_time_column), value) &&
				value <= std::numeric_limits<std::uint32_t>::max())
			{
				entry.usage_time = static_cast<std::uint32_t>(value);
			}

			if (parse_uint(game::string_table::get_cell(periodic, periodic_row->second, periodic_cost_column), value, 16) &&
				value <= std::numeric_limits<std::uint32_t>::max())
			{
				entry.cost_item = static_cast<std::uint32_t>(value);
			}

			entry.price = get_price(game::string_table::get_cell(periodic, periodic_row->second,
				periodic_difficulty_column));
			if (entry.kind == kind_contract && !entry.cost_item)
			{
				continue;
			}

			result.challenges.push_back(std::move(entry));
		}

		if (result.challenges.empty() || result.events.empty())
		{
			return false;
		}

		std::lock_guard lock{catalog_mutex};
		loaded_catalog = std::move(result);
		return true;
	}

	bool handles_action(const std::string_view action)
	{
		return action == "get_scheduled_user_achievements" || action == "activate_scheduled_user_achievement" ||
			action == "activate_user_contract" || action == "deactivate_user_achievement" ||
			action == "claim_achievement_reward" || action == "get_expired_user_achievements" ||
			action == "pump_global_achievement_counters";
	}

	std::vector<contract_sku> get_contract_skus()
	{
		std::lock_guard lock{catalog_mutex};
		std::vector<contract_sku> result{};
		if (!loaded_catalog)
		{
			return result;
		}

		for (const auto* entry : get_offers(*loaded_catalog, static_cast<std::uint64_t>(time(nullptr))))
		{
			if (entry->kind == kind_contract)
			{
				result.push_back({contract_sku_base + entry->id, "c:" + std::to_string(entry->id), entry->price,
					entry->cost_item});
			}
		}

		return result;
	}

	std::optional<action_result> handle_action(const std::string_view action, const std::string_view client_transaction,
		const rapidjson::Value& request)
	{
		if (action == "get_expired_user_achievements" || action == "pump_global_achievement_counters")
		{
			auto response = make_response(action, client_transaction, true);
			if (action == "get_expired_user_achievements")
			{
				response.AddMember("Achievements", rapidjson::Value{rapidjson::kArrayType}, response.GetAllocator());
			}

			return action_result{serialize(response)};
		}

		std::lock_guard lock{catalog_mutex};
		if (!loaded_catalog)
		{
			return action_result{serialize(make_response(action, client_transaction, false))};
		}

		if (action == "get_scheduled_user_achievements")
		{
			return get_scheduled(*loaded_catalog, action, client_transaction);
		}

		if (!request.HasMember("AchievementName") || !request["AchievementName"].IsString())
		{
			return action_result{serialize(make_response(action, client_transaction, false))};
		}

		const std::string_view name{request["AchievementName"].GetString(), request["AchievementName"].GetStringLength()};
		if (action == "activate_scheduled_user_achievement" || action == "activate_user_contract")
		{
			return activate(*loaded_catalog, action, client_transaction, name);
		}

		if (action == "deactivate_user_achievement")
		{
			return deactivate(action, client_transaction, name);
		}

		if (action == "claim_achievement_reward")
		{
			return claim(action, client_transaction, name);
		}

		return std::nullopt;
	}

	std::vector<std::string> handle_game_event(const reward_game_events::event& event)
	{
		std::vector<std::string> pushes{};
		std::lock_guard lock{catalog_mutex};
		if (!loaded_catalog)
		{
			return pushes;
		}

		const auto event_id = loaded_catalog->events.find(event.name);
		if (event_id == loaded_catalog->events.end())
		{
			return pushes;
		}

		const auto now = static_cast<std::uint64_t>(time(nullptr));
		for (const auto& record : achievement_store::get_all())
		{
			if (record.status != achievement_status::in_progress || !is_active(record, now))
			{
				continue;
			}

			const auto* entry = find_challenge(*loaded_catalog, record.name);
			if (!entry || entry->event_id != event_id->second || !matches(*entry, event))
			{
				continue;
			}

			achievement_record updated{};
			const auto status = achievement_store::mutate(record.name, [&](achievement_record& value)
			{
				if (value.status != achievement_status::in_progress)
				{
					return false;
				}

				value.progress = std::min<std::uint16_t>(value.progress + 1, value.progress_target);
				if (value.progress >= value.progress_target)
				{
					value.status = achievement_status::claimable;
				}

				updated = value;
				return true;
			});

			if (status != achievement_store::mutation_result::updated)
			{
				continue;
			}

			const auto claimable = updated.status == achievement_status::claimable;
			pushes.push_back(make_push(updated, claimable ? "claimable" : "inProgress", "CHALLENGE"));
			console::demonware("[DW] challenge: '%s' progress %u/%u\n", updated.name.data(), updated.progress,
				updated.progress_target);
		}

		return pushes;
	}
}
