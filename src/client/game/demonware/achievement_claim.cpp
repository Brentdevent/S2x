#include <std_include.hpp>
#include "achievement_claim.hpp"
#include "achievement_store.hpp"
#include "supply_drop_inventory.hpp"
#include "game/types/demonware.hpp"

#include <map>
#include <set>

namespace demonware::achievement_claim
{
	namespace
	{
		using namespace marketplace_store;
		using allocator = rapidjson::Document::AllocatorType;

		bool unique_object(const rapidjson::Value& value)
		{
			if (!value.IsObject())
			{
				return false;
			}
			std::set<std::string_view> names;
			for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it)
			{
				if (!names.emplace(it->name.GetString(), it->name.GetStringLength()).second)
				{
					return false;
				}
			}
			return true;
		}

		bool ascii(const rapidjson::Value& value, const std::size_t maximum)
		{
			if (!value.IsString() || !value.GetStringLength() || value.GetStringLength() > maximum)
			{
				return false;
			}
			const std::string_view text{value.GetString(), value.GetStringLength()};
			return std::ranges::all_of(text, [](const unsigned char c) { return c >= 0x20 && c <= 0x7e; });
		}

		std::string encode(const rapidjson::Value& value)
		{
			rapidjson::StringBuffer buffer;
			rapidjson::Writer<rapidjson::StringBuffer> writer{buffer};
			value.Accept(writer);
			return {buffer.GetString(), buffer.GetSize()};
		}

		bool amount(const rapidjson::Value& value, const char* field, std::map<std::uint32_t, std::uint32_t>& amounts)
		{
			if (!unique_object(value) || !value.HasMember("id") || !value["id"].IsUint() || !value["id"].GetUint() ||
				!value.HasMember(field) || !value[field].IsUint() || !value[field].GetUint())
			{
				return false;
			}
			auto& total = amounts[value["id"].GetUint()];
			const auto increment = value[field].GetUint();
			if (increment > INT32_MAX || total > INT32_MAX - increment)
			{
				return false;
			}
			total += increment;
			return true;
		}

		bool read_rewards(const std::string& json, std::map<std::uint32_t, std::uint32_t>& items,
			std::map<std::uint32_t, std::uint32_t>& currencies)
		{
			rapidjson::Document rewards;
			rewards.Parse(json.data(), json.size());
			if (rewards.HasParseError() || !rewards.IsArray() || rewards.Empty() || rewards.Size() > 50)
			{
				return false;
			}
			for (const auto& reward : rewards.GetArray())
			{
				if (!unique_object(reward) || reward.MemberCount() != 2 || !reward.HasMember("type") ||
					!reward["type"].IsString())
				{
					return false;
				}
				const std::string_view type{reward["type"].GetString(), reward["type"].GetStringLength()};
				if (type == "grant_currency")
				{
					if (!reward.HasMember("currency") || !amount(reward["currency"], "amount", currencies))
					{
						return false;
					}
				}
				else if (type == "grant_product")
				{
					if (!reward.HasMember("product"))
					{
						return false;
					}
					const auto& product = reward["product"];
					if (!unique_object(product) || product.MemberCount() != 3 || !product.HasMember("id") ||
						!product["id"].IsUint() || !product.HasMember("items") || !product["items"].IsArray() ||
						!product.HasMember("currencies") || !product["currencies"].IsArray())
					{
						return false;
					}
					for (const auto& item : product["items"].GetArray())
					{
						// The restored retail descriptors expand products to actual inventory IDs.
						// Rental/usage overrides require a separate, verified settlement path.
						if (!amount(item, "quantity", items) || item.MemberCount() != 4 ||
							!item.HasMember("usage_duration") || !item["usage_duration"].IsNull() ||
							!item.HasMember("override_usage_duration") || !item["override_usage_duration"].IsInt() ||
							item["override_usage_duration"].GetInt() != 0)
						{
							return false;
						}
					}
					for (const auto& currency : product["currencies"].GetArray())
					{
						if (!amount(currency, "amount", currencies))
						{
							return false;
						}
					}
				}
				else
				{
					return false;
				}
			}
			// Native 0x27DC10 projects currency 1 into inventoryTotalXP; 0xD71B0
			// combines it with match XP and inventoryXPAtLastReset for soldier rank.
			// Currency 7 uses the same absolute wallet callback; stock socialscoreutils
			// derives Social Rank from it and handles the inventory balance event.
			// Division/weapon XP is not this currency. Keep those types unsupported.
			return (!items.empty() || !currencies.empty()) && items.size() <= 50 && currencies.size() <= 13 &&
				std::ranges::all_of(currencies, [](const auto& entry) { return entry.first == 1 || entry.first == 6 || entry.first == 7; });
		}

		rapidjson::Value inventory_row(const inventory_record& item, allocator& alloc)
		{
			rapidjson::Value row{rapidjson::kObjectType};
			row.AddMember("item_id", item.item_id, alloc);
			row.AddMember("item_quantity", item.quantity, alloc);
			row.AddMember("collision_field", item.collision_field, alloc);
			row.AddMember("mod_date_time", item.mod_date_time, alloc);
			rapidjson::Value expiry;
			if (item.expiry_duration)
			{
				expiry.SetUint64(item.expiry_duration);
			}
			row.AddMember("expiry_duration", expiry, alloc);
			return row;
		}

		bool settle(transaction& economy, const achievement_record& record, const bool grant, const achievement_record* bonus,
			const std::uint64_t user, const std::uint32_t timestamp, std::string& response)
		{
			std::map<std::uint32_t, std::uint32_t> items, currencies;
			if (!read_rewards(record.success_rewards, items, currencies) ||
				(bonus && !read_rewards(bonus->success_rewards, items, currencies)))
			{
				return false;
			}
			// The captured Zombies offers establish AC and products, not XP/Social Rank.
			if (achievement_kind::zombies(record.kind) &&
				std::ranges::any_of(currencies, [](const auto& entry) { return entry.first != 6; }))
			{
				return false;
			}
			rapidjson::Document push;
			push.SetObject();
			auto& alloc = push.GetAllocator();
			push.CopyFrom(serialize_achievement(record, alloc), alloc);
			if (!push.IsObject())
			{
				return false;
			}
			push.AddMember("reason", "completed", alloc);
			push.AddMember("type", "CHALLENGE", alloc);
			rapidjson::Value inventory{rapidjson::kObjectType}, details{rapidjson::kArrayType}, balances{rapidjson::kArrayType};
			for (const auto& [id, quantity] : items)
			{
				const auto owned = economy.get_inventory(id);
				auto item = owned.value_or(inventory_record{});
				if ((item.player_id && item.player_id != user) ||
					(!item.account_type.empty() && item.account_type != "steam"))
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
				details.PushBack(inventory_row(item, alloc), alloc);
			}
			for (const auto& [id, amount] : currencies)
			{
				const auto currency = static_cast<std::uint8_t>(id);
				const auto before = economy.get_currency(currency);
				const auto delta = grant ? amount : 0;
				if (before > INT32_MAX - delta || (grant && economy.add_currency(currency, delta) != edit_result::updated))
				{
					return false;
				}
				rapidjson::Value row{rapidjson::kObjectType};
				row.AddMember("currency_id", id, alloc);
				row.AddMember("balance_before", before, alloc);
				row.AddMember("balance_delta", delta, alloc);
				balances.PushBack(row, alloc);
			}
			inventory.AddMember("detailed_inventory", details, alloc);
			inventory.AddMember("currencies", balances, alloc);
			rapidjson::Value trigger{rapidjson::kObjectType}, triggers{rapidjson::kArrayType};
			trigger.AddMember("type", rapidjson::Value{items.empty() ? "GRANT_CURRENCY" : "GRANT_PRODUCT", alloc}, alloc);
			trigger.AddMember("inventory", inventory, alloc);
			triggers.PushBack(trigger, alloc);
			push.AddMember("triggers", triggers, alloc);
			response = encode(push);
			return response.size() <= max_response_json_length;
		}

		bool refresh_replay(std::string& response, const std::uint64_t user, const std::string& name)
		{
			rapidjson::Document push;
			push.Parse(response.data(), response.size());
			if (push.HasParseError() || !push.IsObject() || !push.HasMember("triggers") ||
				!push["triggers"].IsArray() || push["triggers"].Size() != 1)
			{
				return false;
			}
			if (!push.HasMember("activationTimestamp") || !push["activationTimestamp"].IsUint64())
			{
				return false;
			}
			const auto records = achievement_store::get_all();
			const auto current_order = std::ranges::find(records, name, &achievement_record::name);
			if (current_order == records.end())
			{
				return false;
			}
			if (current_order->activation_timestamp != push["activationTimestamp"].GetUint64())
			{
				// Acknowledge the old claim, but never finish a later activation with
				// its obsolete terminal push. The service's normal fetch restores the
				// current native achievement cache; the reward is already persisted.
				response.clear();
				return true;
			}
			auto& trigger = push["triggers"][0];
			if (!trigger.IsObject() || !trigger.HasMember("inventory"))
			{
				return false;
			}
			auto& inventory = trigger["inventory"];
			if (!inventory.IsObject() || !inventory.HasMember("detailed_inventory") ||
				!inventory["detailed_inventory"].IsArray() || !inventory.HasMember("currencies") ||
				!inventory["currencies"].IsArray())
			{
				return false;
			}
			const auto current = get_snapshot();
			if (current.status != store_status::ready)
			{
				return false;
			}
			for (auto& row : inventory["detailed_inventory"].GetArray())
			{
				if (!row.IsObject() || !row.HasMember("item_id") || !row["item_id"].IsUint())
				{
					return false;
				}
				const auto id = row["item_id"].GetUint();
				const auto found = std::ranges::find(current.inventory, id, &inventory_record::item_id);
				auto item = found == current.inventory.end() ? inventory_record{} : *found;
				if ((item.player_id && item.player_id != user) ||
					(!item.account_type.empty() && item.account_type != "steam") || item.quantity > INT32_MAX)
				{
					return false;
				}
				item.item_id = id;
				row = inventory_row(item, push.GetAllocator());
			}
			for (auto& row : inventory["currencies"].GetArray())
			{
				if (!row.IsObject() || !row.HasMember("currency_id") || !row["currency_id"].IsUint() ||
					(row["currency_id"].GetUint() != 1 && row["currency_id"].GetUint() != 6 && row["currency_id"].GetUint() != 7) || !row.HasMember("balance_before") || !row.HasMember("balance_delta"))
				{
					return false;
				}
				const auto found = std::ranges::find(current.currencies, static_cast<std::uint8_t>(row["currency_id"].GetUint()), &currency_record::currency_id);
				const auto balance = found == current.currencies.end() ? 0 : found->value;
				if (balance > INT32_MAX)
				{
					return false;
				}
				row["balance_before"].SetUint(balance);
				row["balance_delta"].SetUint(0);
			}
			// Native parsers replace quantities and set balance_before + balance_delta.
			// A replay must not resurrect spent crates or overwrite a newer wallet.
			response = encode(push);
			return response.size() <= max_response_json_length;
		}
	}

	bool settle_reward(marketplace_store::transaction& economy, const achievement_record& record,
		const bool grant, const std::uint64_t user, const std::uint32_t timestamp, std::string& response)
	{
		return settle(economy, record, grant, nullptr, user, timestamp, response);
	}

	result process(const std::string& json, const std::uint64_t user_id, const std::uint32_t timestamp)
	{
		using namespace game::demonware;
		result response{BD_REWARD_EVENTS_DATA_ERROR};
		rapidjson::Document request;
		request.Parse(json.data(), json.size());
		if (!user_id || !timestamp || json.size() > 6143 || request.HasParseError() || !unique_object(request) ||
			request.MemberCount() != 4 || !request.HasMember("Version") || !request["Version"].IsInt() || request["Version"].GetInt() ||
			!request.HasMember("Action") || !request["Action"].IsString() || request["Action"] != action ||
			!request.HasMember("ClientTx") || !ascii(request["ClientTx"], 24) || request["ClientTx"].GetStringLength() != 24 ||
			!request.HasMember("AchievementName") || !ascii(request["AchievementName"], 100))
		{
			return response;
		}
		const std::string client_tx{request["ClientTx"].GetString(), 24};
		const std::string name{request["AchievementName"].GetString()};
		request.RemoveMember("AchievementName");
		request.AddMember("Status", "error", request.GetAllocator());
		response.acknowledgement = encode(request);
		const auto committed = achievement_store::claim_order(name, user_id, client_tx, timestamp,
			[&](transaction& economy, const achievement_record& record, const bool grant, const achievement_record* bonus, std::string& receipt)
			{
				return settle(economy, record, grant, bonus, user_id, timestamp, receipt);
			});
		if (committed.status != transaction_status::committed && committed.status != transaction_status::replayed)
		{
			// The stock failure callback handles all backend errors without finishing
			// the Order. These are conservative local errors, not recovered retail codes.
			response.error = committed.status == transaction_status::rejected ?
				BD_REWARD_EVENTS_DATA_ERROR : BD_REWARD_EVENTS_TRANSACTION_ERROR;
			return response;
		}
		response.achievement_push = committed.response_json;
		if (committed.status == transaction_status::replayed && !refresh_replay(response.achievement_push, user_id, name))
		{
			response.error = BD_REWARD_EVENTS_TRANSACTION_ERROR;
			return response;
		}
		request["Status"].SetString("ok");
		response.acknowledgement = encode(request);
		response.error = 0;
		return response;
	}
}
