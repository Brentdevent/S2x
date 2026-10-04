#include <std_include.hpp>

#include "achievement_store.hpp"
#include "hq_rewards.hpp"
#include "marketplace_store.hpp"
#include "reward_json.hpp"

#include <utils/finally.hpp>

#include <ranges>

namespace demonware
{
	namespace
	{
		constexpr std::array<std::pair<std::string_view, achievement_status>, 4> status_names
		{{
			{"inactive", achievement_status::inactive},
			{"inProgress", achievement_status::in_progress},
			{"claimable", achievement_status::claimable},
			{"finished", achievement_status::finished},
		}};
	}

	const char* get_achievement_status_name(const achievement_status status)
	{
		const auto entry = std::ranges::find(status_names, status, &std::pair<std::string_view, achievement_status>::second);
		return entry != status_names.end() ? entry->first.data() : "finished";
	}

	std::optional<achievement_status> parse_achievement_status(const std::string_view name)
	{
		const auto entry = std::ranges::find(status_names, name, &std::pair<std::string_view, achievement_status>::first);
		if (entry == status_names.end())
		{
			return std::nullopt;
		}

		return entry->second;
	}

	rapidjson::Value serialize_achievement(const achievement_record& record,
		rapidjson::Document::AllocatorType& allocator)
	{
		rapidjson::Document rewards{};
		rewards.Parse(record.success_rewards.data(), record.success_rewards.size());
		if (rewards.HasParseError() || !rewards.IsArray())
		{
			return {};
		}

		rapidjson::Value value{rapidjson::kObjectType};

		const auto add_optional = [&]<typename T>(const char* name, const std::optional<T>& field)
		{
			rapidjson::Value encoded{};
			if (field)
			{
				encoded.Set(*field);
			}

			value.AddMember(rapidjson::StringRef(name), encoded, allocator);
		};

		rapidjson::Value completion_timestamp{};
		if (record.completion_timestamp)
		{
			completion_timestamp.SetUint64(record.completion_timestamp);
		}

		value.AddMember("status", rapidjson::StringRef(get_achievement_status_name(record.status)), allocator);
		value.AddMember("completionTimestamp", completion_timestamp, allocator);
		value.AddMember("kind", record.kind, allocator);
		reward_json::add_string(value, "name", record.name, allocator);
		value.AddMember("successRewards", rapidjson::Value{rewards, allocator}, allocator);
		add_optional("globalProgressTarget", record.global_progress_target);
		value.AddMember("requiresClaim", record.requires_claim, allocator);
		value.AddMember("completionCount", record.fulfilled_times, allocator);
		add_optional("usageTimeTarget", record.usage_time_target);
		add_optional("activationTimestamp", record.activation_timestamp);
		value.AddMember("progress", record.progress, allocator);
		add_optional("expirationTimestamp", record.expiration_timestamp);
		add_optional("globalCounterID", record.global_counter_id);
		value.AddMember("progressTarget", record.progress_target, allocator);
		add_optional("usageTimeRemaining", record.usage_time_remaining);

		return value;
	}
}

namespace demonware::achievement_store
{
	namespace
	{
		constexpr auto maximum_activation_receipts = marketplace_store::max_processed_transactions;
		constexpr std::size_t transaction_length = 24;
		constexpr std::uint64_t payroll_cooldown = 4 * 60 * 60;

		struct activation_receipt
		{
			std::string name{};
			int kind{};
			std::uint32_t cost_item_id{};
		};

		using receipt_key = std::pair<std::uint64_t, std::string>;

		std::mutex achievement_mutex{};
		std::map<std::string, achievement_record> achievements{};
		std::map<receipt_key, activation_receipt> activation_receipts{};
		bool achievements_loaded{};
		bool achievements_valid{true};

		std::uint64_t now()
		{
			return static_cast<std::uint64_t>(time(nullptr));
		}

		using reward_json::find_member;

		std::string read_string(const rapidjson::Value& value)
		{
			return std::string{reward_json::view(value)};
		}

		template <typename T>
		bool read_optional(const rapidjson::Value& object, const char* name, std::optional<T>& result)
		{
			const auto* value = find_member(object, name);
			if (!value || (!value->IsNull() && !value->Is<T>()))
			{
				return false;
			}

			if (!value->IsNull())
			{
				result = value->Get<T>();
			}

			return true;
		}

		std::optional<achievement_record> read_achievement(const rapidjson::Value& value)
		{
			if (!reward_json::unique_members(value))
			{
				return std::nullopt;
			}

			const auto* name = find_member(value, "name");
			const auto* progress = find_member(value, "progress");
			const auto* kind = find_member(value, "kind");
			const auto* completion_count = find_member(value, "completionCount");
			const auto* progress_target = find_member(value, "progressTarget");
			const auto* completion_timestamp = find_member(value, "completionTimestamp");
			const auto* requires_claim = find_member(value, "requiresClaim");
			const auto* success_rewards = find_member(value, "successRewards");

			if (!name || !name->IsString() || !name->GetStringLength() ||
				!progress || !progress->IsUint() || progress->GetUint() > UINT16_MAX ||
				!kind || !kind->IsInt() ||
				!completion_count || !completion_count->IsInt() ||
				!progress_target || !progress_target->IsUint() || !progress_target->GetUint() ||
				!completion_timestamp || (!completion_timestamp->IsNull() && !completion_timestamp->IsUint64()) ||
				!requires_claim || !requires_claim->IsBool() ||
				!success_rewards || !success_rewards->IsArray())
			{
				return std::nullopt;
			}

			const auto* status_name = find_member(value, "status");
			const auto status = status_name && status_name->IsString()
				? parse_achievement_status(reward_json::view(*status_name))
				: std::nullopt;

			if (!status)
			{
				return std::nullopt;
			}

			achievement_record record{};
			record.name = read_string(*name);
			record.progress = static_cast<std::uint16_t>(progress->GetUint());
			record.kind = kind->GetInt();
			record.fulfilled_times = completion_count->GetInt();
			record.progress_target = progress_target->GetUint();
			record.completion_timestamp = completion_timestamp->IsNull() ? 0 : completion_timestamp->GetUint64();
			record.status = *status;
			record.requires_claim = requires_claim->GetBool();
			record.success_rewards = reward_json::encode(*success_rewards);

			if (!read_optional(value, "activationTimestamp", record.activation_timestamp) ||
				!read_optional(value, "expirationTimestamp", record.expiration_timestamp) ||
				!read_optional(value, "usageTimeTarget", record.usage_time_target) ||
				!read_optional(value, "usageTimeRemaining", record.usage_time_remaining) ||
				!read_optional(value, "globalProgressTarget", record.global_progress_target) ||
				!read_optional(value, "globalCounterID", record.global_counter_id))
			{
				return std::nullopt;
			}

			return record;
		}

		bool read_receipt(const rapidjson::Value& value)
		{
			if (!value.IsObject() || (value.MemberCount() != 4 && value.MemberCount() != 5))
			{
				return false;
			}

			const auto* user_id = find_member(value, "userID");
			const auto* transaction = find_member(value, "transaction");
			const auto* name = find_member(value, "name");
			const auto* kind = find_member(value, "kind");

			if (!user_id || !user_id->IsUint64() || !user_id->GetUint64() ||
				!transaction || !transaction->IsString() || transaction->GetStringLength() != transaction_length ||
				!name || !name->IsString() ||
				!kind || !kind->IsInt())
			{
				return false;
			}

			const auto receipt_kind = kind->GetInt();
			const auto* cost_item = find_member(value, "costItemID");
			const auto cost_item_id = cost_item && cost_item->IsUint() ? cost_item->GetUint() : 0u;

			const auto valid_token = achievement_kind::contract(receipt_kind)
				? cost_item_id != 0
				: achievement_kind::order(receipt_kind) && !cost_item;

			if (!valid_token)
			{
				return false;
			}

			auto receipt_name = read_string(*name);

			const auto achievement = achievements.find(receipt_name);
			if (achievement == achievements.end() || achievement->second.kind != receipt_kind)
			{
				return false;
			}

			receipt_key key{user_id->GetUint64(), read_string(*transaction)};
			activation_receipt receipt{std::move(receipt_name), receipt_kind, cost_item_id};

			return activation_receipts.emplace(std::move(key), std::move(receipt)).second;
		}

		bool read_state()
		{
			const auto state = marketplace_store::get_achievement_state();
			if (state.status != marketplace_store::store_status::ready)
			{
				return false;
			}

			rapidjson::Document document{};
			document.Parse(state.json.data(), state.json.size());
			if (document.HasParseError() || !reward_json::unique_members(document))
			{
				return false;
			}

			const auto* records = find_member(document, "achievements");
			const auto* receipts = find_member(document, "orderActivations");

			if (!records || !records->IsArray() ||
				!receipts || !receipts->IsArray() || receipts->Size() > maximum_activation_receipts)
			{
				return false;
			}

			for (const auto& value : records->GetArray())
			{
				auto record = read_achievement(value);
				if (!record)
				{
					return false;
				}

				auto name = record->name;
				if (!achievements.emplace(std::move(name), std::move(*record)).second)
				{
					return false;
				}
			}

			for (const auto& value : receipts->GetArray())
			{
				if (!read_receipt(value))
				{
					return false;
				}
			}

			return true;
		}

		void load_achievements()
		{
			if (achievements_loaded)
			{
				return;
			}

			achievements_loaded = true;
			achievements_valid = read_state();
		}

		rapidjson::Value serialize_receipt(const receipt_key& key, const activation_receipt& receipt,
			rapidjson::Document::AllocatorType& allocator)
		{
			rapidjson::Value value{rapidjson::kObjectType};
			value.AddMember("userID", key.first, allocator);
			reward_json::add_string(value, "transaction", key.second, allocator);
			reward_json::add_string(value, "name", receipt.name, allocator);
			value.AddMember("kind", receipt.kind, allocator);

			if (receipt.cost_item_id)
			{
				value.AddMember("costItemID", receipt.cost_item_id, allocator);
			}

			return value;
		}

		std::optional<std::string> serialize_state()
		{
			rapidjson::Document document{};
			document.SetObject();

			auto& allocator = document.GetAllocator();

			rapidjson::Value records{rapidjson::kArrayType};
			for (const auto& record : achievements | std::views::values)
			{
				auto value = serialize_achievement(record, allocator);
				if (!value.IsObject())
				{
					return std::nullopt;
				}

				records.PushBack(value, allocator);
			}

			rapidjson::Value receipts{rapidjson::kArrayType};
			for (const auto& [key, receipt] : activation_receipts)
			{
				auto value = serialize_receipt(key, receipt, allocator);
				receipts.PushBack(value, allocator);
			}

			document.AddMember("achievements", records, allocator);
			document.AddMember("orderActivations", receipts, allocator);

			return reward_json::encode(document);
		}

		bool save_achievements()
		{
			const auto json = serialize_state();
			return json && marketplace_store::save_achievement_state(*json);
		}

		void restore(const std::string& name, const std::optional<achievement_record>& original)
		{
			if (original)
			{
				achievements.insert_or_assign(name, *original);
			}
			else
			{
				achievements.erase(name);
			}
		}

		bool is_payroll(const std::string_view name)
		{
			return name == "payroll_officer" || name == "payroll_officer_masterprestige";
		}

		bool is_active(const achievement_status status)
		{
			return status == achievement_status::in_progress || status == achievement_status::claimable;
		}

		std::uint64_t last_payroll_timestamp()
		{
			std::uint64_t last{};
			for (const auto& [name, record] : achievements)
			{
				if (is_payroll(name))
				{
					last = std::max(last, record.completion_timestamp);
				}
			}

			return last;
		}

		bool is_payroll_ready(const std::uint64_t last, const std::uint64_t timestamp, const std::uint64_t event_timestamp)
		{
			if (!last)
			{
				return true;
			}

			// The event time also gates, so a stale queued pickup cannot pay out after a long retry
			return timestamp >= last && timestamp - last >= payroll_cooldown &&
				event_timestamp >= last && event_timestamp - last >= payroll_cooldown;
		}

		// Retail kind-5 descriptors, matching dwGameChallenges IDs 370/371 and AboveAndBeyondTarget
		achievement_record meta_order(const int kind)
		{
			const auto daily = kind == 1;

			achievement_record record{};
			record.name = daily ? "above_beyond_daily" : "above_beyond_weekly";
			record.kind = 5;
			record.progress_target = daily ? 6 : 3;
			record.fulfilled_times = 0;
			record.status = achievement_status::in_progress;
			record.success_rewards = daily
				? R"([{"type":"grant_product","product":{"id":1,"items":[{"id":1,"quantity":1,"usage_duration":null,"override_usage_duration":0}],"currencies":[]}}])"
				: R"([{"type":"grant_product","product":{"id":2,"items":[{"id":2,"quantity":1,"usage_duration":null,"override_usage_duration":0}],"currencies":[]}}])";

			return record;
		}

		bool is_valid_meta_order(const achievement_record& meta, const achievement_record& definition)
		{
			return meta.kind == 5 && meta.status == achievement_status::in_progress && !meta.requires_claim &&
				meta.progress_target == definition.progress_target && meta.progress < meta.progress_target &&
				meta.fulfilled_times >= 0 && meta.fulfilled_times != INT32_MAX &&
				!meta.activation_timestamp && !meta.expiration_timestamp &&
				!meta.usage_time_target && !meta.usage_time_remaining &&
				!meta.global_counter_id && !meta.global_progress_target;
		}

		// Stock emits redeemed_challenge after a claim, so the meta counter settles here instead of from UI events
		bool advance_meta_order(const int kind, const std::uint64_t timestamp, achievement_record*& bonus)
		{
			const auto definition = meta_order(kind);

			auto& meta = achievements.try_emplace(definition.name, definition).first->second;
			if (!is_valid_meta_order(meta, definition))
			{
				return false;
			}

			if (++meta.progress < meta.progress_target)
			{
				return true;
			}

			meta.progress = 0;
			++meta.fulfilled_times;
			meta.completion_timestamp = timestamp;
			bonus = &meta;

			return true;
		}

		bool is_supported_claim(const achievement_record& record)
		{
			if (achievement_kind::order(record.kind))
			{
				return !record.usage_time_target && !record.usage_time_remaining;
			}

			if (achievement_kind::contract(record.kind))
			{
				return record.usage_time_target.value_or(0) > 0 && record.usage_time_remaining.value_or(0) > 0 &&
					*record.usage_time_remaining <= *record.usage_time_target;
			}

			return false;
		}

		bool is_claimable(const achievement_record& record)
		{
			return is_supported_claim(record) && record.requires_claim &&
				record.activation_timestamp && record.completion_timestamp &&
				!record.expiration_timestamp &&
				!record.global_progress_target && !record.global_counter_id &&
				record.progress_target && record.progress >= record.progress_target &&
				(record.status == achievement_status::claimable || record.status == achievement_status::finished);
		}

		const activation_receipt* find_previous_activation(const std::uint64_t user_id, const std::string& name,
			const int kind)
		{
			for (const auto& [key, receipt] : activation_receipts)
			{
				if (key.first == user_id && receipt.name == name && receipt.kind == kind)
				{
					return &receipt;
				}
			}

			return nullptr;
		}

		bool is_new_period(const achievement_record& record, const std::optional<order_offer>& offer,
			const std::uint64_t timestamp)
		{
			// Weapon unlocks stay one-time; bundled bribe offers can recur in later rotations.
			return !(achievement_kind::special(record.kind) && record.status == achievement_status::finished &&
				(!offer || !offer->repeatable)) &&
				offer && offer->period_start &&
				record.activation_timestamp.value_or(timestamp) < offer->period_start &&
				(record.status == achievement_status::finished || record.status == achievement_status::inactive);
		}

		std::optional<activation_result> check_offer(const std::string& name, const int kind,
			const std::optional<order_offer>& offer, const std::uint64_t timestamp)
		{
			if (!offer || offer->achievement.name != name || offer->achievement.kind != kind ||
				offer->next_period_start <= timestamp || offer->period_start > timestamp)
			{
				return activation_result::not_scheduled;
			}

			const auto active = std::ranges::count_if(achievements | std::views::values, [kind](const auto& record)
			{
				return record.kind == kind && is_active(record.status);
			});

			if (active >= offer->activation_limit)
			{
				return activation_result::limit_reached;
			}

			return std::nullopt;
		}

		std::optional<activation_result> check_carry_over(const achievement_record& record, const int kind,
			const bool previously_activated)
		{
			if (achievement_kind::contract(kind) && !previously_activated)
			{
				return activation_result::invalid_state;
			}

			if (record.status == achievement_status::finished)
			{
				return activation_result::already_completed;
			}

			if (!is_active(record.status))
			{
				return activation_result::invalid_state;
			}

			return std::nullopt;
		}

		achievement_record start_record(achievement_record record, const std::uint64_t timestamp)
		{
			record.status = achievement_status::in_progress;
			record.progress = 0;
			record.fulfilled_times = 0;
			record.completion_timestamp = 0;
			record.activation_timestamp = timestamp;

			return record;
		}

		// Stock buys one permanent token, other variants have no verified settlement
		bool is_permanent_token(const marketplace_store::inventory_record& token, const std::uint64_t user_id)
		{
			return marketplace_store::is_permanent(token) && token.quantity == 1 && token.player_id == user_id &&
				token.account_type == "steam" && !token.collision_field;
		}

		activation_result settle_contract(const std::uint64_t user_id, const std::string& transaction,
			const std::string& name, const std::uint32_t token_id, const bool activating)
		{
			const auto json = serialize_state();
			auto failure = activation_result::save_failed;

			const auto settled = marketplace_store::transact("contract:" + transaction,
				"activate_contract:" + std::to_string(user_id) + ":" + name,
				[&](marketplace_store::transaction& economy, std::string& response)
			{
				if (activating)
				{
					const auto token = economy.get_inventory(token_id);
					if (!token || !is_permanent_token(*token, user_id))
					{
						failure = activation_result::missing_token;
						return false;
					}

					if (economy.consume_inventory(token_id, 1) != marketplace_store::edit_result::updated)
					{
						return false;
					}
				}

				if (!json || !economy.set_achievement_state(*json))
				{
					return false;
				}

				response = "{}";
				return true;
			});

			switch (settled.status)
			{
			case marketplace_store::transaction_status::committed:
				return activation_result::success;
			case marketplace_store::transaction_status::client_tx_conflict:
				return activation_result::transaction_conflict;
			default:
				return failure;
			}
		}

		bool is_valid_completion_bits(const achievement_record& bits)
		{
			return !bits.name.empty() && bits.kind == 5 && bits.progress_target &&
				bits.progress_target <= UINT16_MAX && bits.progress &&
				!(bits.progress & ~bits.progress_target);
		}

		bool apply_completion_bits(achievement_record& record, const achievement_record& bits)
		{
			const auto progress = static_cast<std::uint16_t>(record.progress | bits.progress);
			const auto complete = (progress & bits.progress_target) == bits.progress_target;
			const auto status = complete ? achievement_status::finished : achievement_status::in_progress;
			const auto fulfilled_times = complete ? 1 : 0;

			auto changed = record.name != bits.name || record.kind != bits.kind || record.progress != progress ||
				record.progress_target != bits.progress_target || record.status != status ||
				record.fulfilled_times != fulfilled_times;

			record.name = bits.name;
			record.kind = bits.kind;
			record.progress = progress;
			record.progress_target = bits.progress_target;
			record.status = status;
			record.fulfilled_times = fulfilled_times;

			if (complete && !record.completion_timestamp)
			{
				record.completion_timestamp = now();
				changed = true;
			}

			return changed;
		}

		void append_missing(std::vector<achievement_record>& result, achievement_record record)
		{
			if (!achievements.contains(record.name))
			{
				result.push_back(std::move(record));
			}
		}
	}

	activation_result activate_order(const std::string& name, const int kind,
		const std::uint64_t user_id, const std::string& transaction,
		const std::optional<order_offer>& offer, const std::uint64_t timestamp, std::uint32_t* cost_item_id)
	{
		std::lock_guard lock{achievement_mutex};
		load_achievements();

		if (!achievements_valid || !user_id || transaction.size() != transaction_length || name.empty() ||
			!achievement_kind::periodic(kind) || !timestamp)
		{
			return activation_result::invalid_state;
		}

		const receipt_key key{user_id, transaction};

		const auto receipt = activation_receipts.find(key);
		if (receipt != activation_receipts.end() && (receipt->second.name != name || receipt->second.kind != kind))
		{
			return activation_result::transaction_conflict;
		}

		const auto contract = achievement_kind::contract(kind);
		const auto* previous = contract ? find_previous_activation(user_id, name, kind) : nullptr;
		const auto token_id = previous ? previous->cost_item_id : (offer ? offer->cost_item_id : 0);

		if (contract && !token_id)
		{
			return activation_result::not_scheduled;
		}

		if (cost_item_id)
		{
			*cost_item_id = token_id;
		}

		const auto existing = achievements.find(name);
		const auto exists = existing != achievements.end();

		if (exists && existing->second.kind != kind)
		{
			return activation_result::invalid_state;
		}

		if (receipt != activation_receipts.end())
		{
			return exists ? activation_result::success : activation_result::invalid_state;
		}

		const auto activating = !exists || is_new_period(existing->second, offer, timestamp) ||
			(!contract && existing->second.status == achievement_status::inactive);
		const auto rejection = activating
			? check_offer(name, kind, offer, timestamp)
			: check_carry_over(existing->second, kind, previous != nullptr);

		if (rejection)
		{
			return *rejection;
		}

		if (activation_receipts.size() >= maximum_activation_receipts)
		{
			return activation_result::invalid_state;
		}

		const auto original = exists ? std::optional{existing->second} : std::nullopt;

		if (activating)
		{
			achievements.insert_or_assign(name, start_record(offer->achievement, timestamp));
		}

		activation_receipts.emplace(key, activation_receipt{name, kind, token_id});

		auto result = activation_result::success;
		if (contract)
		{
			result = settle_contract(user_id, transaction, name, token_id, activating);
		}
		else if (!save_achievements())
		{
			result = activation_result::save_failed;
		}

		if (result != activation_result::success)
		{
			activation_receipts.erase(key);
			restore(name, original);
		}

		return result;
	}

	marketplace_store::transaction_result deactivate_order(const std::string& name,
		const std::uint64_t user_id, const std::string& transaction)
	{
		using marketplace_store::transaction_status;

		std::lock_guard lock{achievement_mutex};
		load_achievements();

		const auto found = achievements.find(name);
		if (!achievements_valid || !user_id || transaction.size() != transaction_length ||
			found == achievements.end() || !achievement_kind::order(found->second.kind))
		{
			return {transaction_status::rejected};
		}

		const auto original = found->second;
		auto committed = false;
		const auto rollback = utils::finally([&]
		{
			if (!committed)
			{
				found->second = original;
			}
		});

		auto result = marketplace_store::transact("order_abandon:" + transaction,
			"abandon_order:" + std::to_string(user_id) + ":" + name,
			[&](marketplace_store::transaction& economy, std::string& response)
		{
			auto& record = found->second;
			if (record.status != achievement_status::in_progress && record.status != achievement_status::inactive)
			{
				return false;
			}

			record.status = achievement_status::inactive;
			record.progress = 0;
			const auto json = serialize_state();
			response = "{}";
			return json && economy.set_achievement_state(*json);
		});

		committed = result.status == transaction_status::committed;
		return result;
	}

	marketplace_store::transaction_result claim_order(const std::string& name,
		const std::uint64_t user_id, const std::string& transaction, const std::uint64_t timestamp,
		const std::function<bool(marketplace_store::transaction&, const achievement_record&,
			bool, const achievement_record*, std::string&)>& reward)
	{
		using marketplace_store::transaction_status;

		std::lock_guard lock{achievement_mutex};
		load_achievements();

		const auto found = achievements.find(name);
		if (!achievements_valid || !user_id || transaction.size() != transaction_length || !timestamp || !reward ||
			found == achievements.end())
		{
			return {transaction_status::rejected};
		}

		const auto original = achievements;

		auto committed = false;
		const auto rollback = utils::finally([&]
		{
			if (!committed)
			{
				achievements = original;
			}
		});

		auto result = marketplace_store::transact("order_claim:" + transaction,
			"claim_order:" + std::to_string(user_id) + ":" + name,
			[&](marketplace_store::transaction& economy, std::string& response)
		{
			auto& record = found->second;
			if (!is_claimable(record))
			{
				return false;
			}

			const auto granting = record.status == achievement_status::claimable;
			record.status = achievement_status::finished;

			achievement_record* bonus{};
			if (granting && (record.kind == 1 || record.kind == 2) && !advance_meta_order(record.kind, timestamp, bonus))
			{
				return false;
			}

			if (!reward(economy, record, granting, bonus, response))
			{
				return false;
			}

			const auto json = serialize_state();
			return json && economy.set_achievement_state(*json);
		});

		committed = result.status == transaction_status::committed;
		return result;
	}

	bool complete_hq_reward(achievement_record record, const std::uint64_t timestamp,
		const std::uint64_t event_timestamp, const bool payroll,
		const std::function<bool(marketplace_store::transaction&, const achievement_record&, bool)>& reward)
	{
		std::lock_guard lock{achievement_mutex};
		load_achievements();

		if (!achievements_valid || !timestamp || !reward || record.kind != 5)
		{
			return false;
		}

		if (const auto found = achievements.find(record.name); found != achievements.end())
		{
			record = found->second;
		}

		const auto last = payroll
			? std::max(record.completion_timestamp, last_payroll_timestamp())
			: record.completion_timestamp;

		const auto grant = payroll
			? is_payroll_ready(last, timestamp, event_timestamp)
			: record.fulfilled_times == 0;

		if (grant)
		{
			if (record.fulfilled_times == INT_MAX)
			{
				return false;
			}

			++record.fulfilled_times;
			record.completion_timestamp = timestamp;
			record.progress = 1;
			record.status = payroll ? achievement_status::in_progress : achievement_status::finished;
		}
		else if (payroll)
		{
			record.completion_timestamp = last;
			record.fulfilled_times = std::max(1, record.fulfilled_times);
		}

		const auto original = achievements;
		achievements[record.name] = record;

		auto committed = false;
		const auto rollback = utils::finally([&]
		{
			if (!committed)
			{
				achievements = original;
			}
		});

		const auto json = serialize_state();
		committed = json && marketplace_store::save_achievement_state(*json, [&](marketplace_store::transaction& economy)
		{
			return reward(economy, record, grant);
		});

		return committed;
	}

	std::vector<achievement_record> get_all()
	{
		std::lock_guard lock{achievement_mutex};
		load_achievements();

		if (!achievements_valid)
		{
			return {};
		}

		std::vector<achievement_record> result{};
		result.reserve(achievements.size());

		for (const auto& record : achievements | std::views::values)
		{
			result.push_back(record);
		}

		append_missing(result, meta_order(1));
		append_missing(result, meta_order(2));

		for (auto& record : hq_rewards::initial_records())
		{
			append_missing(result, std::move(record));
		}

		// The kiosk switches between both payroll IDs on prestige, so both share the persisted cooldown
		std::uint64_t last_payroll{};
		for (const auto& record : result)
		{
			if (is_payroll(record.name))
			{
				last_payroll = std::max(last_payroll, record.completion_timestamp);
			}
		}

		if (!last_payroll)
		{
			return result;
		}

		for (auto& record : result)
		{
			if (is_payroll(record.name))
			{
				record.completion_timestamp = last_payroll;
				record.fulfilled_times = std::max(1, record.fulfilled_times);
			}
		}

		return result;
	}

	mutation_result merge_completion_bits(const std::vector<achievement_record>& records,
		const std::function<bool(marketplace_store::transaction&)>& reward)
	{
		std::lock_guard lock{achievement_mutex};
		load_achievements();

		if (!achievements_valid || records.empty() || !std::ranges::all_of(records, is_valid_completion_bits))
		{
			return mutation_result::save_failed;
		}

		const auto original = achievements;

		auto changed = false;
		for (const auto& bits : records)
		{
			changed |= apply_completion_bits(achievements[bits.name], bits);
		}

		if (!changed && !reward)
		{
			return mutation_result::unchanged;
		}

		auto committed = false;
		const auto rollback = utils::finally([&]
		{
			if (!committed)
			{
				achievements = original;
			}
		});

		const auto json = serialize_state();
		committed = json && marketplace_store::save_achievement_state(*json, reward);

		return committed ? mutation_result::updated : mutation_result::save_failed;
	}

	mutation_result mutate_all(const std::function<bool(achievement_record&)>& mutator)
	{
		if (!mutator)
		{
			return mutation_result::unchanged;
		}

		std::lock_guard lock{achievement_mutex};
		load_achievements();

		if (!achievements_valid)
		{
			return mutation_result::save_failed;
		}

		auto staged = achievements;

		auto changed = false;
		for (auto& [name, record] : staged)
		{
			changed |= mutator(record);
			record.name = name;
		}

		if (!changed)
		{
			return mutation_result::unchanged;
		}

		achievements.swap(staged);

		auto committed = false;
		const auto rollback = utils::finally([&]
		{
			if (!committed)
			{
				achievements.swap(staged);
			}
		});

		committed = save_achievements();
		return committed ? mutation_result::updated : mutation_result::save_failed;
	}

	mutation_result mutate(const std::string& name,
		const std::function<bool(achievement_record&)>& mutator)
	{
		if (name.empty() || !mutator)
		{
			return mutation_result::unchanged;
		}

		std::lock_guard lock{achievement_mutex};
		load_achievements();

		if (!achievements_valid)
		{
			return mutation_result::save_failed;
		}

		const auto entry = achievements.find(name);
		const auto original = entry != achievements.end() ? std::optional{entry->second} : std::nullopt;

		auto updated = original.value_or(achievement_record{});
		updated.name = name;

		if (!mutator(updated))
		{
			return mutation_result::unchanged;
		}

		updated.name = name;
		achievements.insert_or_assign(name, std::move(updated));

		if (save_achievements())
		{
			return mutation_result::updated;
		}

		restore(name, original);
		return mutation_result::save_failed;
	}
}
