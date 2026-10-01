#include <std_include.hpp>
#include "achievement_orders.hpp"
#include "achievement_response.hpp"
#include "marketplace_catalog.hpp"
#include "marketplace_store.hpp"
#include "reward_json.hpp"
#include "game/types/demonware.hpp"

#include "resource.hpp"

#include <utils/nt.hpp>

#include <set>

namespace demonware::achievement_orders
{
	namespace
	{
		using achievement_store::order_offer;
		using reward_json::find_member;

		constexpr std::size_t kind_count = 14;
		constexpr std::uint64_t day = 24 * 60 * 60;
		constexpr std::uint64_t reset_offset = 17 * 60 * 60;

		struct schedule
		{
			bool valid{};
			std::array<std::uint64_t, kind_count> next_period{};
			std::array<std::uint32_t, kind_count> limits{};
			std::vector<order_offer> orders{};
		};

		struct rotation_group
		{
			int kind{};
			std::uint32_t limit{};
			std::vector<std::vector<order_offer>> sets{};
		};

		struct activation_request
		{
			bool contract{};
			std::string name{};
			std::string transaction{};
			int kind{};
		};

		std::string encode_ascii(const rapidjson::Value& value)
		{
			rapidjson::StringBuffer buffer{};
			rapidjson::Writer<rapidjson::StringBuffer, rapidjson::UTF8<>, rapidjson::ASCII<>> writer{buffer};
			value.Accept(writer);

			return {buffer.GetString(), buffer.GetSize()};
		}

		bool is_weekly(const int kind)
		{
			return kind == 2 || kind == 9;
		}

		std::uint32_t orders_per_set(const int kind)
		{
			if (achievement_kind::contract(kind))
			{
				return 9;
			}

			return is_weekly(kind) ? 3 : 6;
		}

		bool read_contract_fields(const rapidjson::Value& value, order_offer& order)
		{
			const auto* cost_item = find_member(value, "costItemID");
			const auto* usage_time = find_member(value, "usageTimeTarget");

			if (!cost_item || !cost_item->IsUint() || !cost_item->GetUint() ||
				!usage_time || !usage_time->IsInt() || usage_time->GetInt() <= 0)
			{
				return false;
			}

			order.cost_item_id = cost_item->GetUint();
			order.achievement.usage_time_target = usage_time->GetInt();
			order.achievement.usage_time_remaining = order.achievement.usage_time_target;

			return true;
		}

		std::optional<order_offer> read_offer(const rapidjson::Value& offers, const std::string& name, const int kind)
		{
			const auto* value = find_member(offers, name.data());
			if (!value || !reward_json::unique_members(*value))
			{
				return std::nullopt;
			}

			const auto* offer_kind = find_member(*value, "kind");
			const auto* progress_target = find_member(*value, "progressTarget");
			const auto* success_rewards = find_member(*value, "successRewards");

			if (!offer_kind || !offer_kind->IsInt() || offer_kind->GetInt() != kind ||
				!progress_target || !progress_target->IsUint() ||
				!progress_target->GetUint() || progress_target->GetUint() > UINT16_MAX ||
				!success_rewards || !success_rewards->IsArray() || success_rewards->Empty())
			{
				return std::nullopt;
			}

			order_offer order{};
			order.achievement.name = name;
			order.achievement.kind = kind;
			order.achievement.progress_target = progress_target->GetUint();
			order.achievement.requires_claim = true;
			order.achievement.success_rewards = encode_ascii(*success_rewards);

			if (achievement_kind::contract(kind))
			{
				if (!read_contract_fields(*value, order))
				{
					return std::nullopt;
				}
			}
			else if (value->HasMember("costItemID") || value->HasMember("usageTimeTarget"))
			{
				return std::nullopt;
			}

			return order;
		}

		std::optional<rotation_group> read_rotation(const rapidjson::Value& group, const rapidjson::Value& offers,
			std::set<std::string>& used)
		{
			if (!reward_json::unique_members(group))
			{
				return std::nullopt;
			}

			const auto* kind = find_member(group, "kind");
			const auto* limit = find_member(group, "activationLimit");
			const auto* sets = find_member(group, "sets");

			if (!kind || !kind->IsInt() || !achievement_kind::periodic(kind->GetInt()) ||
				!limit || !limit->IsUint() || limit->GetUint() != 3 ||
				!sets || !sets->IsArray() || sets->Empty() || sets->Size() > 32)
			{
				return std::nullopt;
			}

			rotation_group rotation{};
			rotation.kind = kind->GetInt();
			rotation.limit = limit->GetUint();

			for (const auto& set : sets->GetArray())
			{
				if (!set.IsArray() || set.Size() != orders_per_set(rotation.kind))
				{
					return std::nullopt;
				}

				std::set<std::string> names{};
				auto& orders = rotation.sets.emplace_back();

				for (const auto& name_value : set.GetArray())
				{
					if (!reward_json::ascii_string(&name_value, 128))
					{
						return std::nullopt;
					}

					std::string name{reward_json::view(name_value)};
					if (!names.emplace(name).second)
					{
						return std::nullopt;
					}

					auto order = read_offer(offers, name, rotation.kind);
					if (!order)
					{
						return std::nullopt;
					}

					used.emplace(std::move(name));
					orders.push_back(std::move(*order));
				}
			}

			return rotation;
		}

		std::optional<std::vector<rotation_group>> load_rotations()
		{
			const auto data = utils::nt::load_resource(DW_ACHIEVEMENT_OFFERS);
			if (data.empty() || data.size() > achievement_response::maximum_response_length)
			{
				return std::nullopt;
			}

			rapidjson::Document document{};
			document.Parse(data.data(), data.size());
			if (document.HasParseError() || !reward_json::unique_members(document))
			{
				return std::nullopt;
			}

			const auto* offers = find_member(document, "offers");
			const auto* rotations = find_member(document, "rotations");

			if (!offers || !reward_json::unique_members(*offers) ||
				!rotations || !rotations->IsArray() || rotations->Size() != 6)
			{
				return std::nullopt;
			}

			std::vector<rotation_group> result{};
			std::set<int> kinds{};
			std::set<std::string> used{};

			for (const auto& group : rotations->GetArray())
			{
				auto rotation = read_rotation(group, *offers, used);
				if (!rotation || !kinds.emplace(rotation->kind).second)
				{
					return std::nullopt;
				}

				result.push_back(std::move(*rotation));
			}

			if (used.size() != offers->MemberCount())
			{
				return std::nullopt;
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

		const order_offer* find_in_rotation(const rotation_group& group, const std::string_view name)
		{
			for (const auto& set : group.sets)
			{
				const auto found = std::ranges::find(set, name, [](const order_offer& order) -> std::string_view
				{
					return order.achievement.name;
				});

				if (found != set.end())
				{
					return &*found;
				}
			}

			return nullptr;
		}

		schedule make_schedule(const std::uint64_t timestamp, const std::string_view retained_contract = {})
		{
			static const auto rotations = load_rotations();

			schedule result{};
			if (!rotations)
			{
				return result;
			}

			for (const auto& group : *rotations)
			{
				const auto weekly = is_weekly(group.kind);
				const auto period = weekly ? 7 * day : day;
				const auto phase = weekly ? 5 * day + reset_offset : reset_offset;

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

				if (retained_contract.empty() || !achievement_kind::contract(group.kind))
				{
					continue;
				}

				const auto in_current = std::ranges::any_of(current, [&](const order_offer& order)
				{
					return order.achievement.name == retained_contract;
				});

				// A token bought before the reset stays redeemable after its offer rotates away
				if (const auto* retained = in_current ? nullptr : find_in_rotation(group, retained_contract))
				{
					append(*retained);
				}
			}

			result.valid = true;
			return result;
		}

		std::optional<std::string_view> offer_status(const order_offer& order, const std::vector<achievement_record>& active,
			const std::uint64_t timestamp)
		{
			const auto existing = std::ranges::find(active, order.achievement.name, &achievement_record::name);
			if (existing == active.end())
			{
				return "available";
			}

			if (existing->kind != order.achievement.kind)
			{
				return std::nullopt;
			}

			switch (existing->status)
			{
			case achievement_status::in_progress:
				return "in_progress";
			case achievement_status::claimable:
				return "claimable";
			default:
				break;
			}

			const auto from_previous_period = order.period_start &&
				existing->activation_timestamp.value_or(timestamp) < order.period_start;

			return from_previous_period ? "available" : "completed";
		}

		std::optional<activation_request> parse_activation(const std::string_view json)
		{
			if (json.empty() || json.size() > achievement_response::maximum_request_length)
			{
				return std::nullopt;
			}

			rapidjson::Document document{};
			document.Parse(json.data(), json.size());
			if (document.HasParseError() || !reward_json::unique_members(document))
			{
				return std::nullopt;
			}

			activation_request request{};
			request.contract = reward_json::equals(find_member(document, "Action"), contract_action);

			const auto action = request.contract ? contract_action : activation_action;
			if (document.MemberCount() != (request.contract ? 4u : 5u) ||
				!reward_json::common_fields(document, action, request.transaction))
			{
				return std::nullopt;
			}

			const auto* name = find_member(document, "AchievementName");
			if (!reward_json::ascii_string(name, 128))
			{
				return std::nullopt;
			}

			request.name = reward_json::view(*name);

			if (request.contract)
			{
				return request;
			}

			const auto* kind = find_member(document, "AchievementKind");
			if (!kind || !kind->IsInt())
			{
				return std::nullopt;
			}

			request.kind = kind->GetInt();
			return request;
		}

		// 0x1222C0 sends only a name for either contract kind
		int resolve_contract_kind(const std::string& name, const std::optional<order_offer>& offer)
		{
			if (offer)
			{
				return offer->achievement.kind;
			}

			const auto records = achievement_store::get_all();
			const auto record = std::ranges::find(records, name, &achievement_record::name);

			return record != records.end() ? record->kind : 0;
		}

		std::uint32_t activation_error(const achievement_store::activation_result result)
		{
			using namespace game::demonware;
			using achievement_store::activation_result;

			switch (result)
			{
			case activation_result::success:
				return 0;
			case activation_result::limit_reached:
				return BD_REWARD_TOO_MANY_ACTIVE_CHALLENGES;
			case activation_result::missing_token:
			case activation_result::not_scheduled:
				return BD_REWARD_CHALLENGE_NOT_SCHEDULED;
			case activation_result::already_completed:
				return BD_REWARD_CHALLENGE_ALREADY_COMPLETED;
			case activation_result::transaction_conflict:
			case activation_result::save_failed:
				return BD_REWARD_EVENTS_TRANSACTION_ERROR;
			default:
				return BD_REWARD_CONFIGURATION_ERROR;
			}
		}

		// 0x13E8B0 applies these absolute inventory rows before the stock activation callback
		std::optional<rapidjson::Value> make_token_inventory(const std::uint32_t token_id, const std::uint64_t timestamp,
			rapidjson::Document::AllocatorType& allocator)
		{
			const auto economy = marketplace_store::get_snapshot();
			if (economy.status != marketplace_store::store_status::ready)
			{
				return std::nullopt;
			}

			const auto token = std::ranges::find(economy.inventory, token_id, &marketplace_store::inventory_record::item_id);
			const auto quantity = token != economy.inventory.end() ? token->quantity : 0;

			rapidjson::Value item{rapidjson::kObjectType};
			item.AddMember("item_id", token_id, allocator);
			item.AddMember("item_quantity", quantity, allocator);
			item.AddMember("collision_field", 0, allocator);
			item.AddMember("expiry_duration", rapidjson::Value{rapidjson::kNullType}, allocator);
			item.AddMember("mod_date_time", timestamp, allocator);

			rapidjson::Value inventory{rapidjson::kArrayType};
			inventory.PushBack(item, allocator);

			return inventory;
		}
	}

	std::optional<order_offer> contract_for_token(const std::uint32_t token, const std::uint64_t timestamp)
	{
		const auto snapshot = make_schedule(timestamp);
		if (!snapshot.valid || !token || !timestamp)
		{
			return std::nullopt;
		}

		std::optional<order_offer> result{};
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
		const auto snapshot = make_schedule(timestamp);
		if (!snapshot.valid || !timestamp)
		{
			return std::nullopt;
		}

		const auto empty = achievement_response::make_empty_scheduled_user_achievements_response(transaction);
		if (!empty)
		{
			return std::nullopt;
		}

		rapidjson::Document response{};
		response.Parse(empty->data(), empty->size());

		auto& allocator = response.GetAllocator();

		for (std::size_t kind = 0; kind < kind_count; ++kind)
		{
			const auto key = std::to_string(kind);
			const auto current = snapshot.next_period[kind] > timestamp;

			response["ActivationLimits"].AddMember(rapidjson::Value{key.data(), allocator}.Move(),
				current ? snapshot.limits[kind] : 0, allocator);
			response["NextPeriodStartTimes"].AddMember(rapidjson::Value{key.data(), allocator}.Move(),
				current ? snapshot.next_period[kind] : 0, allocator);
		}

		const auto active = achievement_store::get_all();

		for (const auto& order : snapshot.orders)
		{
			if (order.next_period_start <= timestamp || (order.cost_item_id && !has_contract_catalog(order.cost_item_id)))
			{
				continue;
			}

			const auto status = offer_status(order, active, timestamp);
			if (!status)
			{
				return std::nullopt;
			}

			auto value = serialize_achievement(order.achievement, allocator);
			if (!value.IsObject())
			{
				return std::nullopt;
			}

			if (order.cost_item_id)
			{
				value.AddMember("costItemID", order.cost_item_id, allocator);
			}

			value["status"].SetString(status->data(), static_cast<rapidjson::SizeType>(status->size()), allocator);
			response["Achievements"].PushBack(value, allocator);
		}

		auto json = encode_ascii(response);
		if (json.size() > achievement_response::maximum_response_length)
		{
			return std::nullopt;
		}

		return json;
	}

	activation_response activate(const std::string_view json, const std::uint64_t user_id, const std::uint64_t timestamp)
	{
		using namespace game::demonware;

		auto request = parse_activation(json);
		if (!request)
		{
			return {BD_REWARD_EVENTS_DATA_ERROR};
		}

		if (!request->contract && !achievement_kind::order(request->kind))
		{
			return {BD_REWARD_EVENTS_NOT_ENABLED};
		}

		const auto snapshot = make_schedule(timestamp, request->contract ? request->name : "");

		std::optional<order_offer> offer{};
		if (snapshot.valid)
		{
			const auto found = std::ranges::find(snapshot.orders, request->name, [](const order_offer& order) -> const std::string&
			{
				return order.achievement.name;
			});

			if (found != snapshot.orders.end())
			{
				offer = *found;
			}
		}

		if (request->contract)
		{
			request->kind = resolve_contract_kind(request->name, offer);
			if (!achievement_kind::contract(request->kind))
			{
				return {BD_REWARD_CHALLENGE_NOT_SCHEDULED};
			}
		}

		std::uint32_t token_id{};
		const auto result = achievement_store::activate_order(request->name, request->kind, user_id,
			request->transaction, offer, timestamp, &token_id);

		if (const auto error = activation_error(result))
		{
			return {error};
		}

		rapidjson::Document response{rapidjson::kObjectType};
		auto& allocator = response.GetAllocator();

		reward_json::add_string(response, "Action", request->contract ? contract_action : activation_action, allocator);
		response.AddMember("Status", "ok", allocator);
		reward_json::add_string(response, "ClientTx", request->transaction, allocator);

		if (request->contract)
		{
			auto inventory = make_token_inventory(token_id, timestamp, allocator);
			if (!inventory)
			{
				return {BD_REWARD_EVENTS_TRANSACTION_ERROR};
			}

			response.AddMember("DetailedInventory", *inventory, allocator);
		}

		return {0, encode_ascii(response)};
	}
}
