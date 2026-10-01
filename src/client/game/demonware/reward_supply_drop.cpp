#include <std_include.hpp>
#include "reward_supply_drop.hpp"
#include "loot_policy.hpp"
#include "zombies_loot_policy.hpp"
#include "loot_compatibility.hpp"
#include "reward_json.hpp"
#include "supply_drop_inventory.hpp"
#include "game/types/demonware.hpp"

namespace demonware::reward_supply_drop
{
	namespace
	{
		std::uint64_t hash_request(const std::string_view client_tx,
			const std::string_view supply_drop_id)
		{
			constexpr std::uint64_t offset = 14695981039346656037ULL;
			constexpr std::uint64_t prime = 1099511628211ULL;
			std::uint64_t result = offset;
			for (const auto character : client_tx)
			{
				result = (result ^ static_cast<unsigned char>(character)) * prime;
			}

			result = (result ^ 0xFFU) * prime;
			for (const auto character : supply_drop_id)
			{
				result = (result ^ static_cast<unsigned char>(character)) * prime;
			}

			return result;
		}

		std::string make_open_supply_drop_response(const std::string_view client_tx,
			const marketplace_store::inventory_record& crate,
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
			rapidjson::Value detailed_inventory{rapidjson::kArrayType};
			reward_json::add_detailed_inventory(detailed_inventory, crate, allocator);
			for (const auto& card : cards)
			{
				rapidjson::Value item{rapidjson::kObjectType};
				item.AddMember("id", card.item_id, allocator);
				item.AddMember("delta", 1, allocator);
				granted_items.PushBack(item, allocator);
			}
			for (const auto& reward : rewards)
				reward_json::add_detailed_inventory(detailed_inventory, reward, allocator);

			response.AddMember("GrantedItems", granted_items, allocator);
			response.AddMember("GrantedCurrencies", rapidjson::Value{rapidjson::kArrayType}, allocator);
			response.AddMember("DetailedInventory", detailed_inventory, allocator);

			rapidjson::StringBuffer buffer{};
			rapidjson::Writer<rapidjson::StringBuffer> writer{buffer};
			response.Accept(writer);
			return {buffer.GetString(), buffer.GetSize()};
		}

	}

	bool parse_request(const std::string_view json, request& parsed)
	{
		if (json.empty() || json.size() > 6143)
		{
			return false;
		}
		rapidjson::Document document;
		document.Parse(json.data(), json.size());
		request value;
		// Preserve successful legacy ClientTx lengths (1..24), but reject duplicate
		// or unknown fields: they cannot safely share the legacy fingerprint.
		if (document.HasParseError() ||
			!reward_json::common_fields(document, action, value.client_tx, false) ||
			document.MemberCount() != 5 || !document.HasMember("SupplyDropID") ||
			!document["SupplyDropID"].IsString() || !document.HasMember("InventoryVersion"))
			return false;
		const auto& version = document["InventoryVersion"];
		if (!version.IsArray() || version.Size() != 2 ||
			!version[rapidjson::SizeType{0}].IsInt() ||
			version[rapidjson::SizeType{0}].GetInt() != -1 ||
			!version[rapidjson::SizeType{1}].IsInt() ||
			version[rapidjson::SizeType{1}].GetInt() != -1)
			return false;
		value.supply_drop_id.assign(document["SupplyDropID"].GetString(),
			document["SupplyDropID"].GetStringLength());
		if (!reward_json::bounded_ascii(value.supply_drop_id, 128, true))
		{
			return false;
		}
		parsed = std::move(value);
		return true;
	}

	marketplace_store::transaction_result process(
		const std::string& client_tx, const std::string& supply_drop_id,
		const std::uint64_t user_id, const std::shared_ptr<const loot_catalog::catalog>& catalog,
		const std::uint32_t modification_time, open_failure& failure, const float rarity_scale)
	{
		const auto fingerprint = std::string{"open_supply_drop|"} + supply_drop_id + "|" +
			std::to_string(user_id);
		return marketplace_store::transact(client_tx, fingerprint,
			[&](marketplace_store::transaction& transaction, std::string& response)
			{
				if (!catalog)
				{
					failure = open_failure::catalog_unavailable;
					return false;
				}
				const auto supply_drop = loot_catalog::find_supply_drop(*catalog, supply_drop_id);
				if (!supply_drop)
				{
					failure = open_failure::missing_supply_drop;
					return false;
				}

				if (!loot_compatibility::is_confirmed_mp_supply_drop(*supply_drop) &&
					!zombies_loot_policy::supports(*supply_drop))
				{
					failure = open_failure::unsupported_supply_drop;
					return false;
				}

				const auto crate_before = transaction.get_inventory(supply_drop->item_id);
				if (!crate_before || crate_before->quantity == 0)
				{
					failure = open_failure::insufficient_quantity;
					return false;
				}

				if ((crate_before->player_id && crate_before->player_id != user_id) ||
					(!crate_before->account_type.empty() && crate_before->account_type != "steam"))
				{
					failure = open_failure::invalid_state;
					return false;
				}
				// Use the same native expiry predicate as Task 96. Locally granted
				// permanent rows use 0/0; replay never reaches this mutation callback.
				if ((crate_before->expire_date_time || crate_before->expiry_duration) &&
					((crate_before->expire_date_time != UINT32_MAX &&
						modification_time >= crate_before->expire_date_time) || !crate_before->expiry_duration))
				{
					failure = open_failure::expired;
					return false;
				}

				auto candidates = loot_policy::eligible_items(*catalog, *supply_drop,
					transaction);
				auto selected_result = loot_policy::select_supply_drop_items(*supply_drop,
					std::move(candidates), hash_request(client_tx, supply_drop_id), rarity_scale);
				if (zombies_loot_policy::supports(*supply_drop))
					selected_result = zombies_loot_policy::select(*catalog, *supply_drop, transaction,
						hash_request(client_tx, supply_drop_id), rarity_scale);
				if (!selected_result)
				{
					failure = open_failure::missing_eligible_items;
					return false;
				}
				auto selected = std::move(*selected_result);

				for (std::size_t index = 0; index < selected.size(); ++index)
				{
					const auto& item = selected[index];
					auto presentation_item = item;
					presentation_item.stock_internal_costume_component =
						loot_compatibility::stock_internal_costume_component(item.item_id);
					presentation_item.azm_consumable =
						loot_compatibility::stock_azm_consumable(item.item_id);
					if (presentation_item.stock_hidden_item || presentation_item.stock_internal_costume_component ||
						(presentation_item.azm_consumable && !zombies_loot_policy::supports(*supply_drop)))
					{
						failure = open_failure::missing_eligible_items;
						return false;
					}

				}

				if (transaction.consume_inventory(supply_drop->item_id, 1) !=
					marketplace_store::edit_result::updated)
				{
					failure = open_failure::invalid_state;
					return false;
				}

				auto crate_after = transaction.get_inventory(supply_drop->item_id)
					.value_or(*crate_before);
				if (crate_before->quantity == 1)
				{
					crate_after.quantity = 0;
				}
				else
				{
					crate_after.mod_date_time = modification_time;
					const auto update_result = transaction.set_inventory(crate_after);
					if (update_result != marketplace_store::edit_result::updated &&
						update_result != marketplace_store::edit_result::unchanged)
					{
						failure = open_failure::invalid_state;
						return false;
					}
				}
				crate_after.mod_date_time = modification_time;

				std::vector<marketplace_store::inventory_record> granted{};
				granted.reserve(selected.size());
				for (const auto& selected_item : selected)
				{
					marketplace_store::inventory_record grant{};
					if (!supply_drop_inventory::grant(transaction, selected_item.item_id,
						user_id, modification_time, grant))
					{
						failure = open_failure::invalid_state;
						return false;
					}

					// A repeated consumable card grants another unit of its exact stock
					// GUID. Publish one final absolute quantity, retaining every card.
					const auto previous = std::ranges::find(granted, grant.item_id,
						&marketplace_store::inventory_record::item_id);
					if (previous == granted.end()) granted.emplace_back(std::move(grant));
					else *previous = std::move(grant);
				}

				response = make_open_supply_drop_response(client_tx, crate_after, selected, granted);
				return true;
			});
	}

	reward::action_result handle(const std::string_view json, const std::uint64_t user_id,
		const std::shared_ptr<const loot_catalog::catalog>& catalog,
		const std::uint32_t modification_time, const float rarity_scale)
	{
		using namespace game::demonware;
		request request;
		if (!parse_request(json, request))
		{
			return {BD_REWARD_EVENTS_DATA_ERROR};
		}
		open_failure failure{};
		auto transaction = process(request.client_tx, request.supply_drop_id, user_id,
			catalog, modification_time, failure, rarity_scale);

		auto result = reward::transaction_response(std::move(transaction));
		if (result.transaction_status == marketplace_store::transaction_status::replayed &&
			!supply_drop_inventory::refresh_replay(result.response_json, user_id))
		{
			result.error = BD_MARKETPLACE_STORAGE_ERROR;
			result.response_json.clear();
		}
		// These action-specific negative mappings remain provisional, as before.
		if (result.error && result.transaction_status !=
			marketplace_store::transaction_status::client_tx_conflict)
		{
			if (failure == open_failure::insufficient_quantity)
				result.error = BD_MARKETPLACE_INSUFFICIENT_ITEM_QUANTITY;
			else if (failure == open_failure::expired)
				result.error = BD_MARKETPLACE_ITEMS_EXPIRED;
			else if (failure == open_failure::unsupported_supply_drop ||
				failure == open_failure::missing_supply_drop ||
				failure == open_failure::missing_eligible_items)
				result.error = BD_REWARD_EVENTS_RULES_ERROR;
		}
		return result;
	}
}
