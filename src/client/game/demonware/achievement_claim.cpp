#include <std_include.hpp>
#include "dw_include.hpp"
#include "achievement_claim.hpp"
#include "achievement_response.hpp"
#include "reward_json.hpp"
#include "reward_push.hpp"
#include "reward_task4.hpp"
#include "supply_drop_inventory.hpp"

#include "component/achievement_sync.hpp"
#include "game/game.hpp"

#include <ranges>
#include <set>

namespace demonware::achievement_claim
{
	namespace
	{
		using namespace marketplace_store;
		using allocator_type = rapidjson::Document::AllocatorType;
		using amount_map = std::map<std::uint32_t, std::uint32_t>;

		constexpr auto claim_action = "claim_achievement_reward";
		constexpr std::size_t maximum_rewards = 50;
		constexpr std::size_t maximum_reward_currencies = 13;

		struct claim_result
		{
			std::uint32_t error{BD_REWARD_EVENTS_DATA_ERROR};
			std::string acknowledgement{};
			std::string achievement_push{};
		};

		bool is_reward_currency(const std::uint32_t id)
		{
			return id == 1 || id == 2 || id == 6 || id == 7;
		}

		bool is_owned_by(const inventory_record& item, const std::uint64_t user)
		{
			return (!item.player_id || item.player_id == user) &&
				(item.account_type.empty() || item.account_type == "steam");
		}

		std::optional<std::uint32_t> positive_uint(const rapidjson::Value& object, const char* name)
		{
			const auto member = object.FindMember(name);
			if (member == object.MemberEnd() || !member->value.IsUint() || !member->value.GetUint())
			{
				return {};
			}

			return member->value.GetUint();
		}

		bool add_amount(const rapidjson::Value& value, const char* field, amount_map& amounts)
		{
			if (!reward_json::unique_members(value))
			{
				return false;
			}

			const auto id = positive_uint(value, "id");
			const auto increment = positive_uint(value, field);
			if (!id || !increment || *increment > INT32_MAX)
			{
				return false;
			}

			auto& total = amounts[*id];
			if (total > INT32_MAX - *increment)
			{
				return false;
			}

			total += *increment;
			return true;
		}

		bool is_plain_product_item(const rapidjson::Value& item)
		{
			const auto usage = item.FindMember("usage_duration");
			const auto override_usage = item.FindMember("override_usage_duration");

			return item.MemberCount() == 4 &&
				usage != item.MemberEnd() && usage->value.IsNull() &&
				override_usage != item.MemberEnd() && override_usage->value.IsInt() && override_usage->value.GetInt() == 0;
		}

		bool read_product(const rapidjson::Value& product, amount_map& items, amount_map& currencies)
		{
			if (!reward_json::unique_members(product) || product.MemberCount() != 3)
			{
				return false;
			}

			const auto id = product.FindMember("id");
			const auto product_items = product.FindMember("items");
			const auto product_currencies = product.FindMember("currencies");
			if (id == product.MemberEnd() || !id->value.IsUint() ||
				product_items == product.MemberEnd() || !product_items->value.IsArray() ||
				product_currencies == product.MemberEnd() || !product_currencies->value.IsArray())
			{
				return false;
			}

			for (const auto& item : product_items->value.GetArray())
			{
				if (!add_amount(item, "quantity", items) || !is_plain_product_item(item))
				{
					return false;
				}
			}

			for (const auto& currency : product_currencies->value.GetArray())
			{
				if (!add_amount(currency, "amount", currencies))
				{
					return false;
				}
			}

			return true;
		}

		bool read_reward(const rapidjson::Value& reward, amount_map& items, amount_map& currencies)
		{
			if (!reward_json::unique_members(reward) || reward.MemberCount() != 2)
			{
				return false;
			}

			const auto type = reward.FindMember("type");
			if (type == reward.MemberEnd() || !type->value.IsString())
			{
				return false;
			}

			const std::string_view name{type->value.GetString(), type->value.GetStringLength()};
			if (name == "grant_currency")
			{
				const auto currency = reward.FindMember("currency");
				return currency != reward.MemberEnd() && add_amount(currency->value, "amount", currencies);
			}

			if (name == "grant_product")
			{
				const auto product = reward.FindMember("product");
				return product != reward.MemberEnd() && read_product(product->value, items, currencies);
			}

			return false;
		}

		bool read_rewards(const std::string& json, amount_map& items, amount_map& currencies)
		{
			rapidjson::Document rewards{};
			rewards.Parse(json.data(), json.size());
			if (rewards.HasParseError() || !rewards.IsArray() || rewards.Empty() || rewards.Size() > maximum_rewards)
			{
				return false;
			}

			for (const auto& reward : rewards.GetArray())
			{
				if (!read_reward(reward, items, currencies))
				{
					return false;
				}
			}

			if (items.empty() && currencies.empty())
			{
				return false;
			}

			return items.size() <= maximum_rewards && currencies.size() <= maximum_reward_currencies &&
				std::ranges::all_of(currencies | std::views::keys, is_reward_currency);
		}

		bool settle_items(transaction& economy, const amount_map& items, const bool grant, const std::uint64_t user,
			const std::uint32_t timestamp, rapidjson::Value& details, allocator_type& allocator)
		{
			for (const auto& [id, quantity] : items)
			{
				const auto owned = economy.get_inventory(id);
				auto item = owned.value_or(inventory_record{});
				if (!is_owned_by(item, user))
				{
					return false;
				}

				item.item_id = id;

				if (grant)
				{
					if ((owned && !supply_drop_inventory::can_stack(item)) || item.quantity > INT32_MAX - quantity)
					{
						return false;
					}

					item.player_id = user;
					item.account_type = "steam";
					item.quantity += quantity;
					item.mod_date_time = timestamp;

					if (economy.set_inventory(item) != edit_result::updated)
					{
						return false;
					}
				}

				details.PushBack(reward_json::inventory_row(item, allocator), allocator);
			}

			return true;
		}

		bool settle_currencies(transaction& economy, const amount_map& currencies, const bool grant,
			rapidjson::Value& balances, allocator_type& allocator)
		{
			for (const auto& [id, amount] : currencies)
			{
				const auto currency = static_cast<std::uint8_t>(id);
				const auto before = economy.get_currency(currency);
				const auto delta = grant ? amount : 0;
				if (before > INT32_MAX - delta)
				{
					return false;
				}

				if (grant && economy.add_currency(currency, delta) != edit_result::updated)
				{
					return false;
				}

				rapidjson::Value row{rapidjson::kObjectType};
				row.AddMember("currency_id", id, allocator);
				row.AddMember("balance_before", before, allocator);
				row.AddMember("balance_delta", delta, allocator);
				balances.PushBack(row, allocator);
			}

			return true;
		}

		bool settle(transaction& economy, const achievement_record& record, const bool grant,
			const achievement_record* bonus, const std::uint64_t user, const std::uint32_t timestamp, std::string& response)
		{
			amount_map items{};
			amount_map currencies{};
			if (!read_rewards(record.success_rewards, items, currencies) ||
				(bonus && !read_rewards(bonus->success_rewards, items, currencies)))
			{
				return false;
			}

			if (achievement_kind::zombies(record.kind) &&
				std::ranges::any_of(currencies | std::views::keys, [](const auto id) { return id != 6; }))
			{
				return false;
			}

			rapidjson::Document push{};
			auto& allocator = push.GetAllocator();
			push.CopyFrom(serialize_achievement(record, allocator), allocator);
			if (!push.IsObject())
			{
				return false;
			}

			rapidjson::Value details{rapidjson::kArrayType};
			rapidjson::Value balances{rapidjson::kArrayType};
			if (!settle_items(economy, items, grant, user, timestamp, details, allocator) ||
				!settle_currencies(economy, currencies, grant, balances, allocator))
			{
				return false;
			}

			rapidjson::Value inventory{rapidjson::kObjectType};
			inventory.AddMember("detailed_inventory", details, allocator);
			inventory.AddMember("currencies", balances, allocator);

			rapidjson::Value trigger{rapidjson::kObjectType};
			trigger.AddMember("type", rapidjson::StringRef(items.empty() ? "GRANT_CURRENCY" : "GRANT_PRODUCT"), allocator);
			trigger.AddMember("inventory", inventory, allocator);

			rapidjson::Value triggers{rapidjson::kArrayType};
			triggers.PushBack(trigger, allocator);

			push.AddMember("reason", "completed", allocator);
			push.AddMember("type", "CHALLENGE", allocator);
			push.AddMember("triggers", triggers, allocator);

			response = reward_json::encode(push);
			return response.size() <= max_response_json_length;
		}

		bool refresh_inventory_rows(rapidjson::Value& rows, const snapshot& current, const std::uint64_t user,
			allocator_type& allocator)
		{
			for (auto& row : rows.GetArray())
			{
				if (!row.IsObject())
				{
					return false;
				}

				const auto id = row.FindMember("item_id");
				if (id == row.MemberEnd() || !id->value.IsUint())
				{
					return false;
				}

				const auto item_id = id->value.GetUint();
				const auto found = std::ranges::find(current.inventory, item_id, &inventory_record::item_id);
				auto item = found == current.inventory.end() ? inventory_record{} : *found;
				if (!is_owned_by(item, user) || item.quantity > INT32_MAX)
				{
					return false;
				}

				item.item_id = item_id;
				row = reward_json::inventory_row(item, allocator);
			}

			return true;
		}

		bool refresh_currency_rows(rapidjson::Value& rows, const snapshot& current)
		{
			for (auto& row : rows.GetArray())
			{
				if (!row.IsObject() || !row.HasMember("balance_before") || !row.HasMember("balance_delta"))
				{
					return false;
				}

				const auto id = row.FindMember("currency_id");
				if (id == row.MemberEnd() || !id->value.IsUint() || !is_reward_currency(id->value.GetUint()))
				{
					return false;
				}

				const auto currency = static_cast<std::uint8_t>(id->value.GetUint());
				const auto found = std::ranges::find(current.currencies, currency, &currency_record::currency_id);
				const auto balance = found == current.currencies.end() ? 0 : found->value;
				if (balance > INT32_MAX)
				{
					return false;
				}

				row["balance_before"].SetUint(balance);
				row["balance_delta"].SetUint(0);
			}

			return true;
		}

		bool refresh_replay(std::string& response, const std::uint64_t user, const std::string& name)
		{
			rapidjson::Document push{};
			push.Parse(response.data(), response.size());
			if (push.HasParseError() || !push.IsObject())
			{
				return false;
			}

			const auto triggers = push.FindMember("triggers");
			const auto activation = push.FindMember("activationTimestamp");
			if (triggers == push.MemberEnd() || !triggers->value.IsArray() || triggers->value.Size() != 1 ||
				activation == push.MemberEnd() || !activation->value.IsUint64())
			{
				return false;
			}

			const auto records = achievement_store::get_all();
			const auto order = std::ranges::find(records, name, &achievement_record::name);
			if (order == records.end())
			{
				return false;
			}

			if (order->activation_timestamp != activation->value.GetUint64())
			{
				response.clear();
				return true;
			}

			auto& trigger = triggers->value[0u];
			if (!trigger.IsObject())
			{
				return false;
			}

			const auto inventory = trigger.FindMember("inventory");
			if (inventory == trigger.MemberEnd() || !inventory->value.IsObject())
			{
				return false;
			}

			const auto details = inventory->value.FindMember("detailed_inventory");
			const auto balances = inventory->value.FindMember("currencies");
			if (details == inventory->value.MemberEnd() || !details->value.IsArray() ||
				balances == inventory->value.MemberEnd() || !balances->value.IsArray())
			{
				return false;
			}

			const auto current = get_snapshot();
			if (current.status != store_status::ready ||
				!refresh_inventory_rows(details->value, current, user, push.GetAllocator()) ||
				!refresh_currency_rows(balances->value, current))
			{
				return false;
			}

			response = reward_json::encode(push);
			return response.size() <= max_response_json_length;
		}

		claim_result claim(const std::string& json, const std::uint64_t user_id, const std::uint32_t timestamp)
		{
			claim_result result{};
			if (!user_id || !timestamp || json.size() > 6143)
			{
				return result;
			}

			rapidjson::Document request{};
			request.Parse(json.data(), json.size());

			std::string client_tx{};
			if (request.HasParseError() || !reward_json::common_fields(request, claim_action, client_tx) ||
				request.MemberCount() != 4)
			{
				return result;
			}

			const auto achievement_name = request.FindMember("AchievementName");
			if (achievement_name == request.MemberEnd() || !achievement_name->value.IsString())
			{
				return result;
			}

			const std::string name{achievement_name->value.GetString(), achievement_name->value.GetStringLength()};
			if (!reward_json::bounded_ascii(name, 100, true))
			{
				return result;
			}

			request.RemoveMember("AchievementName");
			request.AddMember("Status", "error", request.GetAllocator());
			result.acknowledgement = reward_json::encode(request);

			const auto committed = achievement_store::claim_order(name, user_id, client_tx, timestamp,
				[&](transaction& economy, const achievement_record& record, const bool grant,
					const achievement_record* bonus, std::string& receipt)
			{
				return settle(economy, record, grant, bonus, user_id, timestamp, receipt);
			});

			if (committed.status != transaction_status::committed && committed.status != transaction_status::replayed)
			{
				result.error = committed.status == transaction_status::rejected
					? BD_REWARD_EVENTS_DATA_ERROR
					: BD_REWARD_EVENTS_TRANSACTION_ERROR;
				return result;
			}

			result.achievement_push = committed.response_json;
			if (committed.status == transaction_status::replayed && !refresh_replay(result.achievement_push, user_id, name))
			{
				result.error = BD_REWARD_EVENTS_TRANSACTION_ERROR;
				return result;
			}

			request["Status"].SetString("ok");
			result.acknowledgement = reward_json::encode(request);
			result.error = 0;
			return result;
		}

		std::optional<std::string> make_state_push(const achievement_record& record, const char* reason)
		{
			rapidjson::Document push{};
			auto& allocator = push.GetAllocator();
			push.CopyFrom(serialize_achievement(record, allocator), allocator);
			if (!push.IsObject())
			{
				return {};
			}

			push.AddMember("reason", rapidjson::StringRef(reason), allocator);
			push.AddMember("type", "CHALLENGE", allocator);
			push.AddMember("triggers", rapidjson::Value{rapidjson::kArrayType}, allocator);
			return reward_json::encode(push);
		}

		void push_terminal_states(service_server* server, const std::uint64_t user, const std::string& context)
		{
			static std::set<std::pair<std::string, std::uint64_t>> notified_expirations{};

			for (const auto& record : achievement_store::get_all())
			{
				if (!achievement_kind::periodic(record.kind) || !record.activation_timestamp)
				{
					continue;
				}

				const auto expired = record.status == achievement_status::inactive &&
					record.usage_time_remaining == 0 && record.expiration_timestamp;
				const auto completed = record.status == achievement_status::finished && record.completion_timestamp;
				if (!expired && !completed)
				{
					continue;
				}

				const auto key = std::make_pair(record.name, *record.activation_timestamp);
				if (expired && notified_expirations.contains(key))
				{
					continue;
				}

				const auto push = make_state_push(record, expired ? "expired" : "completed");
				if (!push)
				{
					continue;
				}

				send_reward_push(server, BD_REWARD_ACHIEVEMENT_MESSAGE, user, *push, context);
				if (expired)
				{
					notified_expirations.insert(key);
				}
			}
		}

		void push_above_and_beyond(service_server* server, const std::uint64_t user, const std::string& context,
			const std::string& claimed_name)
		{
			const auto records = achievement_store::get_all();
			const auto claimed = std::ranges::find(records, claimed_name, &achievement_record::name);
			if (claimed == records.end() || (claimed->kind != 1 && claimed->kind != 2))
			{
				return;
			}

			const auto meta_name = claimed->kind == 1 ? "above_beyond_daily" : "above_beyond_weekly";
			const auto meta = std::ranges::find(records, meta_name, &achievement_record::name);
			if (meta == records.end())
			{
				return;
			}

			if (const auto push = make_state_push(*meta, meta->progress ? "inProgress" : "completed"))
			{
				send_reward_push(server, BD_REWARD_ACHIEVEMENT_MESSAGE, user, *push, context);
			}
		}
	}

	bool settle_reward(transaction& economy, const achievement_record& record, const bool grant,
		const std::uint64_t user, const std::uint32_t timestamp, std::string& response)
	{
		return settle(economy, record, grant, nullptr, user, timestamp, response);
	}

	bool try_handle(service_server* server, byte_buffer* buffer)
	{
		if (!buffer)
		{
			return false;
		}

		auto input = *buffer;

		std::string context{};
		std::string json{};
		std::uint16_t count{};
		std::int32_t type{};
		if (!input.read_string(&context, 16) || !input.read_uint16(&count) || count != 1 ||
			!input.read_int32(&type) || type != 1 || !input.read_string(&json, 6143))
		{
			return false;
		}

		rapidjson::Document request{};
		request.Parse(json.data(), json.size());

		if (request.HasParseError() || !request.IsObject())
		{
			return false;
		}

		const auto action = request.FindMember("Action");
		if (action == request.MemberEnd() || !action->value.IsString())
		{
			return false;
		}

		const std::string_view action_name{action->value.GetString(), action->value.GetStringLength()};
		const auto fetching = action_name == achievement_response::get_user_achievements_action;
		if (!fetching && action_name != claim_action)
		{
			return false;
		}

		const auto reject = [&](const std::uint32_t error)
		{
			if (fetching)
			{
				return false;
			}

			server->create_reply(4, error).send();
			return true;
		};

		if (context != "s2_steam" || !input.has_only_zero_padding(16))
		{
			return reject(BD_PARAM_PARSE_ERROR);
		}

		const auto user = achievement_sync::local_user_id();
		if (!user || game::environment::is_dedicated())
		{
			return reject(BD_SERVICE_NOT_AVAILABLE);
		}

		if (fetching)
		{
			achievement_response::user_achievements_request query{};
			if (!achievement_response::parse_get_user_achievements_request(json, query))
			{
				return false;
			}

			reward_task4::execution_context execution{};
			execution.user_id = user;
			if (!reward_task4::handle(server, buffer, execution))
			{
				push_terminal_states(server, user, context);
			}

			return true;
		}

		const auto result = claim(json, user, static_cast<std::uint32_t>(time(nullptr)));
		if (result.acknowledgement.empty())
		{
			server->create_reply(4, result.error).send();
			return true;
		}

		server->create_reply(4).send();

		if (!result.error)
		{
			if (!result.achievement_push.empty())
			{
				send_reward_push(server, BD_REWARD_ACHIEVEMENT_MESSAGE, user, result.achievement_push, context);
			}

			const auto name = request.FindMember("AchievementName");
			if (name != request.MemberEnd() && name->value.IsString())
			{
				push_above_and_beyond(server, user, context, name->value.GetString());
			}
		}

		send_reward_push(server, BD_REWARD_EVENT_MESSAGE, user, result.acknowledgement, context);
		achievement_sync::request_refresh();
		return true;
	}
}
