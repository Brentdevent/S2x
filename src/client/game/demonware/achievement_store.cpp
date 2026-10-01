#include <std_include.hpp>

#include "achievement_store.hpp"
#include "hq_rewards.hpp"
#include "marketplace_store.hpp"

#include <utils/finally.hpp>

#include <map>
#include <mutex>
#include <optional>
#include <set>

namespace demonware::achievement_store
{
	namespace
	{
		std::mutex achievement_mutex{};
		std::map<std::string, achievement_record> achievements{};
		bool achievements_loaded{};
		bool achievements_valid{true};
		constexpr auto maximum_activation_receipts = marketplace_store::max_processed_transactions;
		struct activation_receipt
		{
			std::string name;
			int kind{};
			std::uint32_t cost_item_id{};
		};
		std::map<std::pair<std::uint64_t, std::string>, activation_receipt> activation_receipts;

		bool unique_object(const rapidjson::Value& value)
		{
			if (!value.IsObject())
			{
				return false;
			}
			std::set<std::string_view> keys;
			for (auto member = value.MemberBegin(); member != value.MemberEnd(); ++member)
			{
				if (!keys.emplace(member->name.GetString(), member->name.GetStringLength()).second)
				{
					return false;
				}
			}
			return true;
		}

		std::optional<achievement_status> parse_status(const rapidjson::Value& value)
		{
			if (!value.IsString())
			{
				return std::nullopt;
			}

			const std::string_view status{value.GetString(), value.GetStringLength()};
			if (status == "inactive")
			{
				return achievement_status::inactive;
			}

			if (status == "inProgress")
			{
				return achievement_status::in_progress;
			}

			if (status == "claimable")
			{
				return achievement_status::claimable;
			}

			if (status == "finished")
			{
				return achievement_status::finished;
			}

			return std::nullopt;
		}

		template <typename T>
		bool read_optional(const rapidjson::Value& value, const char* name, std::optional<T>& result)
		{
			const auto member = value.FindMember(name);
			if (member == value.MemberEnd())
			{
				return false;
			}
			if (member->value.IsNull())
			{
				return true;
			}
			if (!member->value.Is<T>())
			{
				return false;
			}
			result = member->value.Get<T>();
			return true;
		}

		void load_achievements()
		{
			if (achievements_loaded)
			{
				return;
			}

			achievements_loaded = true;
			const auto economy = marketplace_store::get_achievement_state();
			if (economy.status != marketplace_store::store_status::ready)
			{
				achievements_valid = false;
				return;
			}
			const auto& data = economy.json;

			rapidjson::Document document{};
			document.Parse(data.data(), data.size());
			if (document.HasParseError() || !unique_object(document) ||
				!document.HasMember("achievements") || !document["achievements"].IsArray() ||
				!document.HasMember("orderActivations") || !document["orderActivations"].IsArray())
			{
				achievements_valid = false;
				return;
			}

			for (const auto& value : document["achievements"].GetArray())
			{
				// Persist the same complete descriptor emitted to the native parser.
				// Missing fields from earlier development files are not synthesized.
				if (!unique_object(value) || !value.HasMember("name") || !value["name"].IsString() ||
					!value["name"].GetStringLength() || !value.HasMember("progress") || !value["progress"].IsUint() ||
					value["progress"].GetUint() > UINT16_MAX || !value.HasMember("kind") || !value["kind"].IsInt() ||
					!value.HasMember("completionCount") || !value["completionCount"].IsInt() ||
					!value.HasMember("progressTarget") || !value["progressTarget"].IsUint() || !value["progressTarget"].GetUint() ||
					!value.HasMember("completionTimestamp") ||
					(!value["completionTimestamp"].IsNull() && !value["completionTimestamp"].IsUint64()) ||
					!value.HasMember("status") || !value.HasMember("requiresClaim") || !value["requiresClaim"].IsBool() ||
					!value.HasMember("successRewards") || !value["successRewards"].IsArray())
				{
					achievements_valid = false; return;
				}
				const auto status = parse_status(value["status"]);
				if (!status)
				{
					achievements_valid = false;
					return;
				}
				achievement_record record{};
				record.name.assign(value["name"].GetString(), value["name"].GetStringLength());
				record.progress = static_cast<std::uint16_t>(value["progress"].GetUint());
				record.kind = value["kind"].GetInt();
				record.fulfilled_times = value["completionCount"].GetInt();
				record.progress_target = value["progressTarget"].GetUint();
				if (!value["completionTimestamp"].IsNull())
				{
					record.completion_timestamp = value["completionTimestamp"].GetUint64();
				}
				record.status = *status;
				record.requires_claim = value["requiresClaim"].GetBool();
				if (!read_optional(value, "activationTimestamp", record.activation_timestamp) ||
					!read_optional(value, "expirationTimestamp", record.expiration_timestamp) ||
					!read_optional(value, "usageTimeTarget", record.usage_time_target) ||
					!read_optional(value, "usageTimeRemaining", record.usage_time_remaining) ||
					!read_optional(value, "globalProgressTarget", record.global_progress_target) ||
					!read_optional(value, "globalCounterID", record.global_counter_id))
				{
					achievements_valid = false; continue;
				}
				{
					rapidjson::StringBuffer buffer;
					rapidjson::Writer<rapidjson::StringBuffer> writer{buffer};
					value["successRewards"].Accept(writer);
					record.success_rewards.assign(buffer.GetString(), buffer.GetSize());
				}

				if (!achievements.emplace(record.name, std::move(record)).second)
				{
					achievements_valid = false;
				}
			}
			{
				const auto& receipts = document["orderActivations"];
				if (!receipts.IsArray() || receipts.Size() > maximum_activation_receipts)
				{
					achievements_valid = false;
					return;
				}
				for (const auto& receipt : receipts.GetArray())
				{
					if (!receipt.IsObject() || (receipt.MemberCount() != 4 && receipt.MemberCount() != 5) ||
						!receipt.HasMember("userID") || !receipt["userID"].IsUint64() || !receipt["userID"].GetUint64() ||
						!receipt.HasMember("transaction") || !receipt["transaction"].IsString() ||
						receipt["transaction"].GetStringLength() != 24 ||
						!receipt.HasMember("name") || !receipt["name"].IsString() ||
						!receipt.HasMember("kind") || !receipt["kind"].IsInt())
					{
						achievements_valid = false;
						return;
					}
					const std::string name{receipt["name"].GetString(), receipt["name"].GetStringLength()};
					const auto kind = receipt["kind"].GetInt();
					const auto found = achievements.find(name);
					const auto token = receipt.FindMember("costItemID");
					const auto token_id = token != receipt.MemberEnd() && token->value.IsUint() ? token->value.GetUint() : 0;
					if ((achievement_kind::contract(kind) ? !token_id : !achievement_kind::order(kind) || token != receipt.MemberEnd()) || found == achievements.end() || found->second.kind != kind ||
						!activation_receipts.emplace(std::make_pair(receipt["userID"].GetUint64(),
							std::string{receipt["transaction"].GetString(), 24}), activation_receipt{name, kind, token_id}).second)
					{
						achievements_valid = false;
						return;
					}
				}
			}
		}

		std::optional<std::string> serialize_state()
		{
			rapidjson::Document document{};
			document.SetObject();
			auto& allocator = document.GetAllocator();
			rapidjson::Value array{rapidjson::kArrayType};

			for (const auto& [name, record] : achievements)
			{
				auto value = serialize_achievement(record, allocator);
				if (!value.IsObject())
				{
					return std::nullopt;
				}
				array.PushBack(value, allocator);
			}

			document.AddMember("achievements", array, allocator);
			rapidjson::Value receipts{rapidjson::kArrayType};
			for (const auto& [key, receipt] : activation_receipts)
			{
				rapidjson::Value value{rapidjson::kObjectType};
				value.AddMember("userID", key.first, allocator);
				value.AddMember("transaction", rapidjson::Value{key.second.c_str(), allocator}, allocator);
				value.AddMember("name", rapidjson::Value{receipt.name.c_str(), allocator}, allocator);
				value.AddMember("kind", receipt.kind, allocator);
				if (receipt.cost_item_id)
				{
					value.AddMember("costItemID", receipt.cost_item_id, allocator);
				}
				receipts.PushBack(value, allocator);
			}
			document.AddMember("orderActivations", receipts, allocator);
			rapidjson::StringBuffer buffer{};
			rapidjson::Writer<rapidjson::StringBuffer> writer{buffer};
			document.Accept(writer);
			return std::string{buffer.GetString(), buffer.GetSize()};
		}

		bool save_achievements()
		{
			const auto json = serialize_state();
			return json && marketplace_store::save_achievement_state(*json);
		}

		achievement_record meta_order(const int kind)
		{
			// Retail kind-5 descriptors (2026-08-25 capture), corroborated by native
			// dwGameChallenges IDs 370/371 and AboveAndBeyondTarget in stock LUI.
			achievement_record record;
			record.name = kind == 1 ? "above_beyond_daily" : "above_beyond_weekly";
			record.kind = 5;
			record.progress_target = kind == 1 ? 6 : 3;
			record.fulfilled_times = 0;
			record.status = achievement_status::in_progress;
			record.success_rewards = kind == 1 ?
				R"([{"type":"grant_product","product":{"id":1,"items":[{"id":1,"quantity":1,"usage_duration":null,"override_usage_duration":0}],"currencies":[]}}])" :
				R"([{"type":"grant_product","product":{"id":2,"items":[{"id":2,"quantity":1,"usage_duration":null,"override_usage_duration":0}],"currencies":[]}}])";
			return record;
		}

		void merge_record(achievement_record record, const std::uint64_t timestamp)
		{
			if (record.name.empty())
			{
				return;
			}

			if (!record.progress_target)
			{
				record.progress_target = std::max<std::uint16_t>(record.progress, 1);
			}

			if (record.status == achievement_status::finished && record.fulfilled_times > 0 &&
				!record.completion_timestamp)
			{
				record.completion_timestamp = timestamp;
			}

			achievements[record.name] = std::move(record);
		}
	}

	activation_result activate_order(const std::string& name, const int kind,
		const std::uint64_t user_id, const std::string& transaction,
		const std::optional<order_offer>& offer, const std::uint64_t timestamp, std::uint32_t* cost_item_id)
	{
		std::lock_guard lock{achievement_mutex};
		load_achievements();
		if (!achievements_valid || !user_id || transaction.size() != 24 || name.empty() ||
			!achievement_kind::periodic(kind) || !timestamp)
		{
			return activation_result::invalid_state;
		}

		const auto key = std::make_pair(user_id, transaction);
		const auto receipt = activation_receipts.find(key);
		if (receipt != activation_receipts.end() &&
			(receipt->second.name != name || receipt->second.kind != kind))
		{
			return activation_result::transaction_conflict;
		}

		std::uint32_t token_id = offer ? offer->cost_item_id : 0;
		bool previous_contract_activation{};
		if (achievement_kind::contract(kind))
		{
			// Keep the token identity with the activation, including after rotation.
			const auto previous = std::ranges::find_if(activation_receipts, [&](const auto& entry)
			{
				return entry.first.first == user_id && entry.second.name == name && entry.second.kind == kind;
			});
			if (previous != activation_receipts.end())
			{
				previous_contract_activation = true;
				token_id = previous->second.cost_item_id;
			}
			if (!token_id)
			{
				return activation_result::not_scheduled;
			}
		}
		if (cost_item_id)
		{
			*cost_item_id = token_id;
		}
		const auto existing = achievements.find(name);
		if (existing != achievements.end() && existing->second.kind != kind)
		{
			return activation_result::invalid_state;
		}
		// Replaying an accepted request never reactivates work, including after a
		// local period reset. Its token has already been settled in the same save.
		if (receipt != activation_receipts.end())
		{
			return existing != achievements.end() ? activation_result::success : activation_result::invalid_state;
		}
		const auto repeat = existing != achievements.end() && offer && offer->period_start &&
			existing->second.activation_timestamp.value_or(timestamp) < offer->period_start &&
			(existing->second.status == achievement_status::finished || existing->second.status == achievement_status::inactive);
		const auto activating = existing == achievements.end() || repeat;
		if (!activating)
		{
			if (achievement_kind::contract(kind) && !previous_contract_activation)
			{
				return activation_result::invalid_state;
			}
			if (existing->second.status == achievement_status::finished)
			{
				return activation_result::already_completed;
			}
			if (existing->second.status != achievement_status::in_progress &&
				existing->second.status != achievement_status::claimable)
			{
				return activation_result::invalid_state;
			}
		}
		else
		{
			if (!offer || offer->achievement.name != name || offer->achievement.kind != kind ||
				offer->next_period_start <= timestamp || offer->period_start > timestamp)
			{
				return activation_result::not_scheduled;
			}
			const auto active = std::ranges::count_if(achievements, [kind](const auto& entry)
			{
				return entry.second.kind == kind &&
					(entry.second.status == achievement_status::in_progress ||
						entry.second.status == achievement_status::claimable);
			});
			if (active >= offer->activation_limit)
			{
				return activation_result::limit_reached;
			}
		}
		// Never evict successful transactions: a full ledger fails closed.
		if (activation_receipts.size() >= maximum_activation_receipts)
		{
			return activation_result::invalid_state;
		}

		const auto original = existing == achievements.end() ? std::optional<achievement_record>{} : existing->second;
		const auto rollback = [&]
		{
			if (original)
			{
				achievements[name] = *original;
			}
			else
			{
				achievements.erase(name);
			}
		};
		if (activating)
		{
			auto record = offer->achievement;
			record.status = achievement_status::in_progress;
			record.progress = 0;
			record.fulfilled_times = 0;
			record.completion_timestamp = 0;
			record.activation_timestamp = timestamp;
			achievements.insert_or_assign(name, std::move(record));
		}
		activation_receipts.emplace(key, activation_receipt{name, kind, token_id});
		if (achievement_kind::contract(kind))
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
					// Stock buys one permanent token; collision/expiry variants have
					// no verified settlement and must not be silently consumed.
					if (!token || token->quantity != 1 || token->player_id != user_id ||
						token->account_type != "steam" || token->collision_field ||
						((token->expire_date_time || token->expiry_duration) &&
							(token->expire_date_time != UINT32_MAX || token->expiry_duration != INT64_MAX)))
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
			if (settled.status == marketplace_store::transaction_status::committed)
			{
				return activation_result::success;
			}
			activation_receipts.erase(key);
			rollback();
			return settled.status == marketplace_store::transaction_status::client_tx_conflict ?
				activation_result::transaction_conflict : failure;
		}
		if (save_achievements())
		{
			return activation_result::success;
		}

		activation_receipts.erase(key);
		rollback();
		return activation_result::save_failed;
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
		if (!achievements_valid || !user_id || transaction.size() != 24 || !timestamp || !reward || found == achievements.end())
		{
			return {transaction_status::rejected};
		}
		auto& record = found->second;
		const auto original = achievements;
		bool committed{};
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
				// Receipt identity follows the request, not the current activation.
				// A rotation may have renewed this Order since the original claim.
				// Timed contracts use the same claim transaction after completing before
				// their deadline. Expired/global/recurring achievements remain unsupported.
				const auto supported = (achievement_kind::order(record.kind) &&
					!record.usage_time_target && !record.usage_time_remaining) || (achievement_kind::contract(record.kind) &&
					record.usage_time_target.value_or(0) > 0 && record.usage_time_remaining.value_or(0) > 0 &&
					*record.usage_time_remaining <= *record.usage_time_target);
			if (!supported || !record.requires_claim ||
				!record.activation_timestamp || !record.completion_timestamp ||
				record.expiration_timestamp ||
				record.global_progress_target || record.global_counter_id ||
				!record.progress_target || record.progress < record.progress_target ||
				(record.status != achievement_status::claimable && record.status != achievement_status::finished))
			{
				return false;
			}

			const auto granting = record.status == achievement_status::claimable;

			record.status = achievement_status::finished;
			achievement_record* bonus{};
			if (granting && (record.kind == 1 || record.kind == 2))
			{
				auto definition = meta_order(record.kind);
				auto& meta = achievements.try_emplace(definition.name, definition).first->second;
				if (meta.kind != 5 || meta.status != achievement_status::in_progress || meta.requires_claim ||
					meta.progress_target != definition.progress_target || meta.progress >= meta.progress_target ||
					meta.fulfilled_times < 0 || meta.fulfilled_times == INT32_MAX || meta.activation_timestamp ||
					meta.expiration_timestamp || meta.usage_time_target || meta.usage_time_remaining ||
					meta.global_counter_id || meta.global_progress_target)
				{
					return false;
				}
				// Stock emits redeemed_challenge (17, selector 1 = Order kind) after
				// a successful claim. Settle here instead of counting replayable UI events.
				// Kind 5 has no offer period/expiry. Local recurring policy: carry across
				// rotations and reset the counter only when its automatic reward commits.
				if (++meta.progress == meta.progress_target)
				{
					meta.progress = 0;
					++meta.fulfilled_times;
					meta.completion_timestamp = timestamp;
					bonus = &meta;
				}
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
		const auto original = achievements;
		const auto found = achievements.find(record.name);
		if (found != achievements.end())
		{
			record = found->second;
		}
		auto last = record.completion_timestamp;
		if (payroll)
		{
			for (const auto* name : {"payroll_officer", "payroll_officer_masterprestige"})
			{
				const auto other = achievements.find(name);
				if (other != achievements.end())
				{
					last = std::max(last, other->second.completion_timestamp);
				}
			}
		}
		// A stale queued pickup must not become a new payment after a long retry.
		const auto grant = payroll ? (!last || (timestamp >= last && timestamp - last >= 14400 &&
			event_timestamp >= last && event_timestamp - last >= 14400)) :
			record.fulfilled_times == 0;
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
		achievements[record.name] = record;
		bool committed{};
		const auto rollback = utils::finally([&]
		{
			if (!committed)
			{
				achievements = original;
			}
		});
		const auto json = serialize_state();
		committed = json && marketplace_store::save_achievement_state(*json,
			[&](marketplace_store::transaction& economy) { return reward(economy, record, grant); });
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
		for (const auto& [name, record] : achievements)
		{
			result.push_back(record);
		}

		// The native meta widgets also need zero-progress descriptors on a new
		// profile. They become durable in the first successful daily/weekly claim.
		for (const auto kind : {1, 2})
		{
			auto record = meta_order(kind);
			if (!achievements.contains(record.name))
			{
				result.push_back(std::move(record));
			}
		}
		for (auto record : hq_rewards::initial_records())
		{
			if (!achievements.contains(record.name))
			{
				result.push_back(std::move(record));
			}
		}
		// The kiosk chooses either ID when prestige changes. Project the shared
		// persisted cooldown into both descriptors so its availability stays correct.
		std::uint64_t last_payroll{};
		for (const auto& record : result)
		{
			if (record.name == "payroll_officer" || record.name == "payroll_officer_masterprestige")
			{
				last_payroll = std::max(last_payroll, record.completion_timestamp);
			}
		}
		if (last_payroll)
		{
			for (auto& record : result)
			{
				if (record.name == "payroll_officer" || record.name == "payroll_officer_masterprestige")
				{
					record.completion_timestamp = last_payroll;
					record.fulfilled_times = std::max(1, record.fulfilled_times);
				}
			}
		}
		return result;
	}

	bool merge(const std::vector<achievement_record>& records)
	{
		std::lock_guard lock{achievement_mutex};
		load_achievements();
		if (!achievements_valid)
		{
			return false;
		}

		const auto original = achievements;
		const auto timestamp = static_cast<std::uint64_t>(time(nullptr));
		for (auto record : records)
		{
			merge_record(std::move(record), timestamp);
		}

		if (save_achievements())
		{
			return true;
		}

		achievements = original;
		return false;
	}

	mutation_result merge_completion_bits(const std::vector<achievement_record>& records,
		const std::function<bool(marketplace_store::transaction&)>& reward)
	{
		std::lock_guard lock{achievement_mutex};
		load_achievements();
		if (!achievements_valid || records.empty())
		{
			return mutation_result::save_failed;
		}
		const auto original = achievements;
		bool committed{};
		const auto rollback = utils::finally([&]
		{
			if (!committed)
			{
				achievements = original;
			}
		});
		bool changed{};
		for (const auto& bits : records)
		{
			if (bits.name.empty() || bits.kind != 5 || !bits.progress_target ||
				bits.progress_target > UINT16_MAX || !bits.progress ||
				(bits.progress & ~bits.progress_target))
			{
				return mutation_result::save_failed;
			}
			auto& record = achievements[bits.name];
			const auto progress = static_cast<std::uint16_t>(record.progress | bits.progress);
			const auto complete = (progress & bits.progress_target) == bits.progress_target;
			const auto status = complete ? achievement_status::finished : achievement_status::in_progress;
			changed |= record.name != bits.name || record.kind != bits.kind || record.progress != progress ||
				record.progress_target != bits.progress_target || record.status != status ||
				record.fulfilled_times != (complete ? 1 : 0);
			record.name = bits.name;
			record.kind = bits.kind;
			record.progress = progress;
			record.progress_target = bits.progress_target;
			record.status = status;
			record.fulfilled_times = complete ? 1 : 0;
			if (complete && !record.completion_timestamp)
			{
				record.completion_timestamp = static_cast<std::uint64_t>(time(nullptr));
				changed = true;
			}
		}
		if (!changed && !reward)
		{
			committed = true;
			return mutation_result::unchanged;
		}
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
		bool changed{};
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
		bool committed{};
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
		const auto existed = entry != achievements.end();
		achievement_record original{};
		achievement_record updated{};
		if (existed)
		{
			original = entry->second;
			updated = original;
		}
		else
		{
			updated.name = name;
		}

		if (!mutator(updated))
		{
			return mutation_result::unchanged;
		}

		updated.name = name;
		achievements[name] = std::move(updated);
		if (save_achievements())
		{
			return mutation_result::updated;
		}

		if (existed)
		{
			achievements[name] = std::move(original);
		}
		else
		{
			achievements.erase(name);
		}

		return mutation_result::save_failed;
	}
}

const char* demonware::get_achievement_status_name(const achievement_status status)
{
	switch (status)
	{
	case achievement_status::inactive:
		return "inactive";
	case achievement_status::in_progress:
		return "inProgress";
	case achievement_status::claimable:
		return "claimable";
	case achievement_status::finished:
	default:
		return "finished";
	}
}

rapidjson::Value demonware::serialize_achievement(const achievement_record& record,
	rapidjson::Document::AllocatorType& allocator)
{
	rapidjson::Document rewards;
	rewards.Parse(record.success_rewards.data(), record.success_rewards.size());
	if (rewards.HasParseError() || !rewards.IsArray())
	{
		return {};
	}

	rapidjson::Value value{rapidjson::kObjectType};
	const auto add_optional = [&]<typename T>(const char* name, const std::optional<T>& field)
	{
		rapidjson::Value encoded;
		if (field)
		{
			encoded.Set(*field);
		}
		value.AddMember(rapidjson::Value{name, allocator}, encoded, allocator);
	};
	value.AddMember("status", rapidjson::Value{get_achievement_status_name(record.status), allocator}, allocator);
	if (record.completion_timestamp)
	{
		value.AddMember("completionTimestamp", record.completion_timestamp, allocator);
	}
	else
	{
		value.AddMember("completionTimestamp", rapidjson::Value{rapidjson::kNullType}, allocator);
	}
	value.AddMember("kind", record.kind, allocator);
	value.AddMember("name", rapidjson::Value{record.name.data(),
		static_cast<rapidjson::SizeType>(record.name.size()), allocator}, allocator);
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
