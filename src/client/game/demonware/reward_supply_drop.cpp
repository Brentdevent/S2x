#include <std_include.hpp>
#include "reward_supply_drop.hpp"
#include "loot_compatibility.hpp"
#include "loot_policy.hpp"
#include "reward_json.hpp"
#include "supply_drop_inventory.hpp"
#include "zombies_loot_policy.hpp"

#include "game/types/demonware.hpp"

namespace demonware::reward_supply_drop
{
	namespace
	{
		enum class open_failure
		{
			none,
			unsupported_supply_drop,
			missing_supply_drop,
			insufficient_quantity,
			missing_eligible_items,
			invalid_state,
			catalog_unavailable,
			expired,
		};

		struct request
		{
			std::string client_tx{};
			std::string supply_drop_id{};
		};

		std::uint64_t hash_request(const std::string_view client_tx, const std::string_view supply_drop_id)
		{
			constexpr std::uint64_t prime = 1099511628211ULL;
			std::uint64_t result = 14695981039346656037ULL;

			const auto mix = [&](const std::uint8_t value)
			{
				result = (result ^ value) * prime;
			};

			for (const auto c : client_tx)
			{
				mix(static_cast<std::uint8_t>(c));
			}

			mix(0xFF);

			for (const auto c : supply_drop_id)
			{
				mix(static_cast<std::uint8_t>(c));
			}

			return result;
		}

		bool is_expired(const marketplace_store::inventory_record& crate, const std::uint32_t now)
		{
			if (!crate.expire_date_time && !crate.expiry_duration)
			{
				return false;
			}

			return !crate.expiry_duration || (crate.expire_date_time != UINT32_MAX && now >= crate.expire_date_time);
		}

		bool is_owned_by(const marketplace_store::inventory_record& item, const std::uint64_t user_id)
		{
			return (!item.player_id || item.player_id == user_id) &&
				(item.account_type.empty() || item.account_type == "steam");
		}

		bool is_presentable(const loot_catalog::loot_item& item, const bool zombies)
		{
			if (item.stock_hidden_item || loot_compatibility::stock_internal_costume_component(item.item_id))
			{
				return false;
			}

			return zombies || !loot_compatibility::stock_azm_consumable(item.item_id);
		}

		std::optional<request> parse_request(const std::string_view json)
		{
			if (json.empty() || json.size() > 6143)
			{
				return {};
			}

			rapidjson::Document document{};
			document.Parse(json.data(), json.size());

			request result{};
			if (document.HasParseError() || !reward_json::common_fields(document, action, result.client_tx, false) ||
				document.MemberCount() != 5)
			{
				return {};
			}

			const auto id = document.FindMember("SupplyDropID");
			const auto version = document.FindMember("InventoryVersion");
			if (id == document.MemberEnd() || !id->value.IsString() || version == document.MemberEnd())
			{
				return {};
			}

			const auto& versions = version->value;
			if (!versions.IsArray() || versions.Size() != 2 ||
				!versions[0u].IsInt() || versions[0u].GetInt() != -1 ||
				!versions[1u].IsInt() || versions[1u].GetInt() != -1)
			{
				return {};
			}

			result.supply_drop_id.assign(id->value.GetString(), id->value.GetStringLength());
			if (!reward_json::bounded_ascii(result.supply_drop_id, 128, true))
			{
				return {};
			}

			return result;
		}

		std::string make_response(const std::string_view client_tx, const marketplace_store::inventory_record& crate,
			const std::vector<loot_catalog::loot_item>& cards,
			const std::vector<marketplace_store::inventory_record>& rewards)
		{
			rapidjson::Document response{};
			response.SetObject();

			auto& allocator = response.GetAllocator();
			response.AddMember("Version", 0, allocator);
			response.AddMember("Action", "open_supply_drop", allocator);
			response.AddMember("Status", "ok", allocator);
			reward_json::add_string(response, "ClientTx", client_tx, allocator);

			rapidjson::Value granted_items{rapidjson::kArrayType};
			for (const auto& card : cards)
			{
				rapidjson::Value item{rapidjson::kObjectType};
				item.AddMember("id", card.item_id, allocator);
				item.AddMember("delta", 1, allocator);
				granted_items.PushBack(item, allocator);
			}

			rapidjson::Value detailed_inventory{rapidjson::kArrayType};
			reward_json::add_detailed_inventory(detailed_inventory, crate, allocator);
			for (const auto& reward : rewards)
			{
				reward_json::add_detailed_inventory(detailed_inventory, reward, allocator);
			}

			response.AddMember("GrantedItems", granted_items, allocator);
			response.AddMember("GrantedCurrencies", rapidjson::Value{rapidjson::kArrayType}, allocator);
			response.AddMember("DetailedInventory", detailed_inventory, allocator);

			return reward_json::encode(response);
		}

		marketplace_store::transaction_result open(const request& request, const std::uint64_t user_id,
			const std::shared_ptr<const loot_catalog::catalog>& catalog, const std::uint32_t now,
			const float rarity_scale, open_failure& failure)
		{
			const auto fingerprint = "open_supply_drop|" + request.supply_drop_id + "|" + std::to_string(user_id);

			return marketplace_store::transact(request.client_tx, fingerprint,
				[&](marketplace_store::transaction& transaction, std::string& response)
			{
				const auto fail = [&](const open_failure reason)
				{
					failure = reason;
					return false;
				};

				if (!catalog)
				{
					return fail(open_failure::catalog_unavailable);
				}

				const auto drop = loot_catalog::find_supply_drop(*catalog, request.supply_drop_id);
				if (!drop)
				{
					return fail(open_failure::missing_supply_drop);
				}

				const auto zombies = zombies_loot_policy::supports(*drop);
				if (!zombies && !loot_compatibility::is_confirmed_mp_supply_drop(*drop))
				{
					return fail(open_failure::unsupported_supply_drop);
				}

				const auto crate = transaction.get_inventory(drop->item_id);
				if (!crate || !crate->quantity)
				{
					return fail(open_failure::insufficient_quantity);
				}

				if (!is_owned_by(*crate, user_id))
				{
					return fail(open_failure::invalid_state);
				}

				if (is_expired(*crate, now))
				{
					return fail(open_failure::expired);
				}

				const auto seed = hash_request(request.client_tx, request.supply_drop_id);
				const auto selected = zombies
					? zombies_loot_policy::select(*catalog, *drop, transaction, seed, rarity_scale)
					: loot_policy::select_supply_drop_items(*drop, loot_policy::eligible_items(*catalog, *drop, transaction),
						seed, rarity_scale);

				if (!selected || !std::ranges::all_of(*selected, [&](const auto& item) { return is_presentable(item, zombies); }))
				{
					return fail(open_failure::missing_eligible_items);
				}

				if (transaction.consume_inventory(drop->item_id, 1) != marketplace_store::edit_result::updated)
				{
					return fail(open_failure::invalid_state);
				}

				auto crate_after = transaction.get_inventory(drop->item_id).value_or(*crate);
				if (crate->quantity == 1)
				{
					crate_after.quantity = 0;
				}
				else
				{
					crate_after.mod_date_time = now;

					const auto result = transaction.set_inventory(crate_after);
					if (result != marketplace_store::edit_result::updated && result != marketplace_store::edit_result::unchanged)
					{
						return fail(open_failure::invalid_state);
					}
				}

				crate_after.mod_date_time = now;

				std::vector<marketplace_store::inventory_record> granted{};
				for (const auto& item : *selected)
				{
					marketplace_store::inventory_record grant{};
					if (!supply_drop_inventory::grant(transaction, item.item_id, user_id, now, grant))
					{
						return fail(open_failure::invalid_state);
					}

					const auto previous = std::ranges::find(granted, grant.item_id, &marketplace_store::inventory_record::item_id);
					if (previous == granted.end())
					{
						granted.emplace_back(std::move(grant));
					}
					else
					{
						*previous = std::move(grant);
					}
				}

				response = make_response(request.client_tx, crate_after, *selected, granted);
				return true;
			});
		}

		std::uint32_t failure_error(const open_failure failure, const std::uint32_t fallback)
		{
			using namespace game::demonware;

			switch (failure)
			{
			case open_failure::insufficient_quantity:
				return BD_MARKETPLACE_INSUFFICIENT_ITEM_QUANTITY;
			case open_failure::expired:
				return BD_MARKETPLACE_ITEMS_EXPIRED;
			case open_failure::unsupported_supply_drop:
			case open_failure::missing_supply_drop:
			case open_failure::missing_eligible_items:
				return BD_REWARD_EVENTS_RULES_ERROR;
			default:
				return fallback;
			}
		}
	}

	reward::action_result handle(const std::string_view json, const std::uint64_t user_id,
		const std::shared_ptr<const loot_catalog::catalog>& catalog, const std::uint32_t modification_time,
		const float rarity_scale)
	{
		using namespace game::demonware;

		const auto request = parse_request(json);
		if (!request)
		{
			return {BD_REWARD_EVENTS_DATA_ERROR};
		}

		auto failure = open_failure::none;
		auto result = reward::transaction_response(open(*request, user_id, catalog, modification_time, rarity_scale, failure));

		if (result.transaction_status == marketplace_store::transaction_status::replayed &&
			!supply_drop_inventory::refresh_replay(result.response_json, user_id))
		{
			result.error = BD_MARKETPLACE_STORAGE_ERROR;
			result.response_json.clear();
		}

		if (result.error && result.transaction_status != marketplace_store::transaction_status::client_tx_conflict)
		{
			result.error = failure_error(failure, result.error);
		}

		return result;
	}
}
