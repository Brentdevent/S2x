#include <std_include.hpp>
#include "achievement_orders.hpp"
#include "achievement_response.hpp"
#include "marketplace_store.hpp"
#include "marketplace_catalog.hpp"
#include "game/types/demonware.hpp"

#include "resource.hpp"
#include <utils/nt.hpp>
#include <array>
#include <vector>
#include <set>

namespace demonware::achievement_orders
{
	namespace
	{
		using achievement_store::order_offer;
		struct schedule
		{
			bool valid{};
			std::array<std::uint64_t, 14> next_period{};
			std::array<std::uint32_t, 14> limits{};
			std::vector<order_offer> orders;
		};

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

		bool ascii_string(const rapidjson::Value& value, const std::size_t maximum)
		{
			if (!value.IsString() || !value.GetStringLength() || value.GetStringLength() > maximum)
			{
				return false;
			}
			const std::string_view text{value.GetString(), value.GetStringLength()};
			return std::ranges::all_of(text, [](const unsigned char c) { return c >= 0x21 && c <= 0x7E; });
		}

		bool is_string(const rapidjson::Value& value, const std::string_view text)
		{
			return value.IsString() && std::string_view{value.GetString(), value.GetStringLength()} == text;
		}

		std::string encode(const rapidjson::Value& value)
		{
			rapidjson::StringBuffer buffer;
			rapidjson::Writer<rapidjson::StringBuffer, rapidjson::UTF8<>, rapidjson::ASCII<>> writer{buffer};
			value.Accept(writer);
			return {buffer.GetString(), buffer.GetSize()};
		}

		struct rotation_group
		{
			int kind{};
			std::uint32_t limit{};
			std::vector<std::vector<order_offer>> sets;
		};

		std::optional<std::vector<rotation_group>> load_rotations()
		{
			// Immutable backend policy, not profile state. Captured offers retain
			// backend overrides of table defaults; uncaptured rewards are marked local.
			// Objective predicates, names and presentation still use the stock tables.
			const auto data = utils::nt::load_resource(DW_ACHIEVEMENT_OFFERS);
			if (data.empty() || data.size() > achievement_response::maximum_response_length)
			{
				return {};
			}
			rapidjson::Document document;
			document.Parse(data.data(), data.size());
			if (document.HasParseError() || !unique_object(document) ||
				!document.HasMember("offers") || !unique_object(document["offers"]) ||
				!document.HasMember("rotations") || !document["rotations"].IsArray() ||
				document["rotations"].Size() != 6)
			{
				return {};
			}
			std::vector<rotation_group> result;
			std::set<int> kinds;
			std::set<std::string> used;
			for (const auto& group : document["rotations"].GetArray())
			{
				if (!unique_object(group) || !group.HasMember("kind") || !group["kind"].IsInt() ||
					!achievement_kind::periodic(group["kind"].GetInt()) ||
					!kinds.emplace(group["kind"].GetInt()).second ||
					!group.HasMember("activationLimit") || !group["activationLimit"].IsUint() ||
					group["activationLimit"].GetUint() != 3 ||
					!group.HasMember("sets") || !group["sets"].IsArray() ||
					group["sets"].Empty() || group["sets"].Size() > 32)
				{
					return {};
				}
				rotation_group rotation;
				rotation.kind = group["kind"].GetInt();
				rotation.limit = group["activationLimit"].GetUint();
				for (const auto& set : group["sets"].GetArray())
				{
					const auto count = achievement_kind::contract(rotation.kind) ? 9u : (rotation.kind == 2 || rotation.kind == 9 ? 3u : 6u);
					if (!set.IsArray() || set.Size() != count)
					{
						return {};
					}
					std::set<std::string> names;
					auto& orders = rotation.sets.emplace_back();
					for (const auto& name : set.GetArray())
					{
						if (!ascii_string(name, 128) || !names.emplace(name.GetString()).second)
						{
							return {};
						}
						const auto found = document["offers"].FindMember(name.GetString());
						if (found == document["offers"].MemberEnd())
						{
							return {};
						}
						const auto& value = found->value;
						if (!unique_object(value) || !value.HasMember("kind") || !value["kind"].IsInt() ||
							value["kind"].GetInt() != rotation.kind ||
							!value.HasMember("progressTarget") || !value["progressTarget"].IsUint() ||
							!value["progressTarget"].GetUint() || value["progressTarget"].GetUint() > UINT16_MAX ||
							!value.HasMember("successRewards") || !value["successRewards"].IsArray() ||
							value["successRewards"].Empty())
						{
							return {};
						}
						order_offer order;
						auto& record = order.achievement;
						record.name = name.GetString();
						record.kind = rotation.kind;
						record.progress_target = value["progressTarget"].GetUint();
						record.requires_claim = true;
						record.success_rewards = encode(value["successRewards"]);
						if (achievement_kind::contract(rotation.kind))
						{
							if (!value.HasMember("costItemID") || !value["costItemID"].IsUint() || !value["costItemID"].GetUint() ||
								!value.HasMember("usageTimeTarget") || !value["usageTimeTarget"].IsInt() ||
								value["usageTimeTarget"].GetInt() <= 0)
							{
								return {};
							}
							order.cost_item_id = value["costItemID"].GetUint();
							record.usage_time_target = value["usageTimeTarget"].GetInt();
							record.usage_time_remaining = record.usage_time_target;
						}
						else if (value.HasMember("costItemID") || value.HasMember("usageTimeTarget"))
						{
							return {};
						}
						used.emplace(record.name);
						orders.push_back(std::move(order));
					}
				}
				result.push_back(std::move(rotation));
			}
			if (used.size() != document["offers"].MemberCount())
			{
				return {};
			}
			return result;
		}

		bool has_contract_catalog(const std::uint32_t token)
		{
			const auto& catalog = marketplace_catalog::get_embedded();
			if (!catalog.value)
			{
				return false;
			}
			for (const auto& entry : catalog.value->skus())
			{
				const auto& sku = entry.fields;
				if (sku.sku_type != marketplace_sku::quartermaster_sku_type || sku.sold_out ||
					sku.prices.size() != 1 || sku.prices[0].currency != 6 || !sku.prices[0].value)
				{
					continue;
				}
				const auto product = catalog.value->find_product(sku.field_36);
				if (product && product->fields.items.size() == 1 &&
					product->fields.items[0].first == token && product->fields.items[0].second == 1)
				{
					return true;
				}
			}
			return false;
		}

		schedule offers(const std::uint64_t timestamp, const std::string_view retained_contract = {})
		{
			static const auto rotations = load_rotations();
			schedule result;
			if (!rotations)
			{
				return result;
			}
			for (const auto& group : *rotations)
			{
				// Local fixed rotations use the established UTC resets. Contracts
				// rotate daily, independently of already purchased tokens and progress.
				const bool weekly = group.kind == 2 || group.kind == 9;
				const std::uint64_t period = weekly ? 7 * 86400 : 86400;
				const std::uint64_t phase = weekly ? 5 * 86400 + 17 * 3600 : 17 * 3600;
				if (timestamp < phase || timestamp > UINT64_MAX - period)
				{
					return result;
				}
				const auto index = (timestamp - phase) / period;
				const auto start = phase + index * period;
				result.limits[group.kind] = group.limit;
				result.next_period[group.kind] = start + period;
				const auto append = [&](order_offer order)
				{
					order.activation_limit = group.limit;
					order.period_start = start;
					order.next_period_start = start + period;
					result.orders.push_back(std::move(order));
				};
				const auto& current = group.sets[index % group.sets.size()];
				for (const auto& order : current)
				{
					append(order);
				}
				// A token paid for before reset remains redeemable after its offer
				// rotates away. Only activation requests use this fallback; the locked
				// store still requires ownership and enforces the active limit. New
				// purchases and scheduled menus resolve only the current rotation.
				if (!retained_contract.empty() && achievement_kind::contract(group.kind) &&
					std::ranges::none_of(current, [&](const auto& order)
					{ return order.achievement.name == retained_contract; }))
				{
					for (const auto& set : group.sets)
					{
						const auto found = std::ranges::find_if(set, [&](const auto& order)
						{ return order.achievement.name == retained_contract; });
						if (found != set.end())
						{
							append(*found);
							break;
						}
					}
				}
			}
			result.valid = true;
			return result;
		}
	}

	std::optional<achievement_store::order_offer> contract_for_token(const std::uint32_t token, const std::uint64_t timestamp)
	{
		const auto& snapshot = offers(timestamp);
		if (!snapshot.valid || !token || !timestamp)
		{
			return std::nullopt;
		}
		std::optional<achievement_store::order_offer> result;
		for (const auto& order : snapshot.orders)
		{
			if (!achievement_kind::contract(order.achievement.kind) || order.cost_item_id != token)
			{
				continue;
			}
			if (result || order.next_period_start <= timestamp)
			{
				return std::nullopt;
			}
			result = order;
		}
		return result;
	}

	std::optional<std::string> scheduled_response(const std::string_view transaction, const std::uint64_t timestamp)
	{
		const auto& snapshot = offers(timestamp);
		if (!snapshot.valid || !timestamp)
		{
			return std::nullopt;
		}
		const auto empty = achievement_response::make_empty_scheduled_user_achievements_response(transaction);
		if (!empty)
		{
			return std::nullopt;
		}
		rapidjson::Document response;
		response.Parse(empty->c_str());
		auto& allocator = response.GetAllocator();
		// The stock parser leaves omitted map entries unchanged. Clear unsupported
		// or expired kinds explicitly, including after a previous online session.
		for (int kind = 0; kind < 14; ++kind)
		{
			const auto key = std::to_string(kind);
			const auto current = snapshot.next_period[kind] > timestamp;
			response["ActivationLimits"].AddMember(rapidjson::Value{key.c_str(), allocator}.Move(), current ? snapshot.limits[kind] : 0, allocator);
			response["NextPeriodStartTimes"].AddMember(rapidjson::Value{key.c_str(), allocator}.Move(), current ? snapshot.next_period[kind] : 0, allocator);
		}
		const auto active = achievement_store::get_all();
		for (const auto& order : snapshot.orders)
		{
			const auto& name = order.achievement.name;
			if (order.next_period_start <= timestamp)
			{
				continue;
			}
			// The Contracts menu compares the native SKU price directly. Offers
			// require a captured or explicitly supported local catalog entry.
			if (order.cost_item_id && !has_contract_catalog(order.cost_item_id))
			{
				continue;
			}
			std::string status = "available";
			const auto existing = std::ranges::find(active, name, &achievement_record::name);
			if (existing != active.end())
			{
				if (existing->kind != order.achievement.kind)
				{
					return std::nullopt;
				}
				switch (existing->status)
				{
				case achievement_status::in_progress: status = "in_progress"; break;
				case achievement_status::claimable: status = "claimable"; break;
				default:
					status = order.period_start && existing->activation_timestamp.value_or(timestamp) < order.period_start ?
						"available" : "completed";
					break;
				}
			}
			auto value = serialize_achievement(order.achievement, allocator);
			if (order.cost_item_id && value.IsObject())
			{
				value.AddMember("costItemID", order.cost_item_id, allocator);
			}
			if (!value.IsObject())
			{
				return std::nullopt;
			}
			value["status"].SetString(status.c_str(), allocator);
			response["Achievements"].PushBack(value, allocator);
		}
		auto json = encode(response);
		if (json.size() > achievement_response::maximum_response_length)
		{
			return std::nullopt;
		}
		return json;
	}

	activation_response activate(const std::string_view request, const std::uint64_t user_id, const std::uint64_t timestamp)
	{
		using namespace game::demonware;
		if (request.empty() || request.size() > achievement_response::maximum_request_length)
		{
			return {BD_REWARD_EVENTS_DATA_ERROR};
		}
		rapidjson::Document document;
		document.Parse(request.data(), request.size());
		if (document.HasParseError() || !unique_object(document))
		{
			return {BD_REWARD_EVENTS_DATA_ERROR};
		}
		const auto contract = document.HasMember("Action") && is_string(document["Action"], contract_action);
		if (document.MemberCount() != (contract ? 4u : 5u) ||
			!document.HasMember("Version") || !document["Version"].IsInt() || document["Version"].GetInt() != 0 ||
			!document.HasMember("Action") || !is_string(document["Action"], contract ? contract_action : activation_action) ||
			!document.HasMember("ClientTx") || !ascii_string(document["ClientTx"], 24) || document["ClientTx"].GetStringLength() != 24 ||
			!document.HasMember("AchievementName") || !ascii_string(document["AchievementName"], 128) ||
			(!contract && (!document.HasMember("AchievementKind") || !document["AchievementKind"].IsInt())))
		{
			return {BD_REWARD_EVENTS_DATA_ERROR};
		}
		const std::string name = document["AchievementName"].GetString();
		const std::string transaction = document["ClientTx"].GetString();
		auto kind = contract ? 0 : document["AchievementKind"].GetInt();
		if (!contract && !achievement_kind::order(kind))
		{
			return {BD_REWARD_EVENTS_NOT_ENABLED};
		}
		const auto& snapshot = offers(timestamp, contract ? name : "");
		std::optional<achievement_store::order_offer> offer;
		const auto found = std::ranges::find_if(snapshot.orders, [&name](const auto& order)
		{
			return order.achievement.name == name;
		});
		if (snapshot.valid && found != snapshot.orders.end())
		{
			offer = *found;
		}
		// 0x1222C0 sends only a name for either contract kind (4/11).
		if (contract)
		{
			if (offer)
			{
				kind = offer->achievement.kind;
			}
			else
			{
				const auto records = achievement_store::get_all();
				const auto record = std::ranges::find(records, name, &achievement_record::name);
				if (record != records.end())
				{
					kind = record->kind;
				}
			}
			if (!achievement_kind::contract(kind))
			{
				return {BD_REWARD_CHALLENGE_NOT_SCHEDULED};
			}
		}
		// Replay is checked in the same locked store transaction, before schedule
		// validity/expiry. Rotation must not invalidate an already committed Order.
		// The stock menu explicitly handles 13909 for the activation limit.
		// Other failures use conservative SDK errors, not captured retail mappings.
		using result = achievement_store::activation_result;
		std::uint32_t token_id{};
		switch (achievement_store::activate_order(name, kind, user_id, transaction, offer, timestamp, &token_id))
		{
		case result::success: break;
		case result::limit_reached: return {BD_REWARD_TOO_MANY_ACTIVE_CHALLENGES};
		case result::missing_token: return {BD_REWARD_CHALLENGE_NOT_SCHEDULED};
		case result::not_scheduled: return {BD_REWARD_CHALLENGE_NOT_SCHEDULED};
		case result::already_completed: return {BD_REWARD_CHALLENGE_ALREADY_COMPLETED};
		case result::transaction_conflict:
		case result::save_failed: return {BD_REWARD_EVENTS_TRANSACTION_ERROR};
		default: return {BD_REWARD_CONFIGURATION_ERROR};
		}
		rapidjson::Document response{rapidjson::kObjectType};
		auto& allocator = response.GetAllocator();
		response.AddMember("Action", rapidjson::Value{contract ? contract_action.data() : activation_action.data(), allocator}, allocator);
		response.AddMember("Status", "ok", allocator);
		response.AddMember("ClientTx", rapidjson::Value{transaction.c_str(), allocator}, allocator);
		if (contract)
		{
			// 0x13E8B0 -> 0x27C1D0 updates/removes absolute inventory rows before
			// the stock activation callback. Replays use current persisted quantity.
			const auto economy = marketplace_store::get_snapshot();
			if (economy.status != marketplace_store::store_status::ready)
			{
				return {BD_REWARD_EVENTS_TRANSACTION_ERROR};
			}
			const auto token = std::ranges::find(economy.inventory, token_id, &marketplace_store::inventory_record::item_id);
			rapidjson::Value inventory{rapidjson::kArrayType}, item{rapidjson::kObjectType};
			item.AddMember("item_id", token_id, allocator);
			item.AddMember("item_quantity", token == economy.inventory.end() ? 0 : token->quantity, allocator);
			item.AddMember("collision_field", 0, allocator);
			item.AddMember("expiry_duration", rapidjson::Value{rapidjson::kNullType}, allocator);
			item.AddMember("mod_date_time", timestamp, allocator);
			inventory.PushBack(item, allocator);
			response.AddMember("DetailedInventory", inventory, allocator);
		}
		return {0, encode(response)};
	}
}
