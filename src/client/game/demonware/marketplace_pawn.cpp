#include <std_include.hpp>
#include "marketplace_pawn.hpp"
#include "marketplace_inventory.hpp"
#include "game/types/demonware.hpp"

#include <utils/cryptography.hpp>

namespace demonware::marketplace_pawn
{
	namespace
	{
		using namespace game::demonware;
		struct item_request
		{
			std::uint32_t id{}, quantity{};
			std::uint16_t collision{};
		};

		std::uint32_t error_code(const marketplace_store::transaction_status status, const std::uint32_t error)
		{
			using enum marketplace_store::transaction_status;
			if (status == committed || status == replayed)
			{
				return BD_NO_ERROR;
			}
			if (status == client_tx_conflict)
			{
				return BD_MARKETPLACE_IDEMPOTENT_REQUEST_COLLISION;
			}
			if (status == invalid_argument)
			{
				return BD_MARKETPLACE_INVALID_PARAMETER;
			}
			return status == rejected ? error : BD_MARKETPLACE_STORAGE_ERROR;
		}

		std::uint32_t consume(marketplace_store::transaction& transaction, const item_request& input,
			const pawn_catalog::item& definition, const std::uint64_t user,
			marketplace_store::inventory_record& changed)
		{
			auto item = transaction.get_inventory(input.id);
			// The native duplicate pump (0x276C20) requests quantity - 1.
			// A new ClientTx after an interrupted completion must not sell that copy.
			if (!input.quantity || !item || item->quantity <= input.quantity)
			{
				return BD_MARKETPLACE_INSUFFICIENT_ITEM_QUANTITY;
			}
			if ((item->player_id && item->player_id != user) ||
				(!item->account_type.empty() && item->account_type != "steam"))
			{
				return BD_MARKETPLACE_INVALID_PARAMETER;
			}
			// The local store has one row per GUID. Only its collision-zero row
			// and the SDK's default any-collision request are verified.
			if (item->collision_field || (input.collision && input.collision != UINT16_MAX))
			{
				return BD_MARKETPLACE_RESOURCE_CONFLICT;
			}
			if ((item->expire_date_time || item->expiry_duration) &&
				(item->expire_date_time != UINT32_MAX || item->expiry_duration != INT64_MAX))
			{
				return BD_MARKETPLACE_INVALID_PARAMETER;
			}
			if (definition.currency != 6 || !definition.amount)
			{
				return BD_MARKETPLACE_MISCONFIGURED;
			}
			const auto now = std::time(nullptr);
			if (now <= 0 || static_cast<std::uint64_t>(now) >= UINT32_MAX)
			{
				return BD_MARKETPLACE_STORAGE_ERROR;
			}
			const auto credit = std::uint64_t{input.quantity} * definition.amount;
			if (credit > UINT32_MAX || credit + transaction.get_currency(definition.currency) > INT32_MAX)
			{
				return BD_MARKETPLACE_OVER_ITEM_MAX_QUANTITY_ERROR;
			}
			item->quantity -= input.quantity;
			item->mod_date_time = static_cast<std::uint32_t>(now);
			if (transaction.set_inventory(*item) != marketplace_store::edit_result::updated ||
				transaction.add_currency(definition.currency, static_cast<std::uint32_t>(credit)) !=
					marketplace_store::edit_result::updated)
			{
				return BD_MARKETPLACE_STORAGE_ERROR;
			}
			changed = std::move(*item);
			return BD_NO_ERROR;
		}

		std::string balance_push(const std::uint64_t user, const std::uint32_t balance)
		{
			// 0xA330F0 reads owner/account, count, then currency-byte/uint32
			// pairs (0xA49900). 0x27AC80 clears the pawn task's wallet flag.
			byte_buffer push;
			push.write_uint32(BD_MARKETPLACE_BALANCE_UPDATED);
			push.write_uint64(user);
			push.write_string("steam");
			push.write_uint32(1);
			push.write_ubyte(6);
			push.write_uint32(balance);
			return push.get_buffer();
		}
	}

	std::uint32_t pawn(byte_buffer* buffer, const std::uint64_t user,
		const pawn_catalog::catalog* catalog, std::vector<std::string>& updates)
	{
		updates.clear();
		if (!user)
		{
			return BD_SERVICE_NOT_AVAILABLE;
		}
		std::string context, client_tx;
		std::uint32_t count{};
		if (!buffer || buffer->remaining_size() > 256 || !buffer->read_string(&context, 15) ||
			context != "s2_steam" || !buffer->read_string(&client_tx, 24) || client_tx.empty() ||
			!std::ranges::all_of(client_tx, [](const unsigned char c) { return c >= 0x21 && c <= 0x7E; }) ||
			!buffer->read_uint32(&count) || !count || count > 10)
		{
			return BD_MARKETPLACE_INVALID_PARAMETER;
		}
		// SDK task 199 (0xA42CA0), item serializer 0xA4B050.
		std::vector<item_request> items(count);
		std::string fingerprint = std::to_string(user) + ":";
		std::unordered_set<std::uint32_t> ids;
		for (auto& item : items)
		{
			if (!buffer->read_uint32(&item.id) || !item.id || !ids.insert(item.id).second ||
				!buffer->read_uint32(&item.quantity) || !item.quantity || !buffer->read_uint16(&item.collision))
			{
				return BD_MARKETPLACE_INVALID_PARAMETER;
			}
			fingerprint += std::to_string(item.id) + ":" + std::to_string(item.quantity) + ":" +
				std::to_string(item.collision) + ";";
		}
		if (!buffer->has_only_zero_padding(16))
		{
			return BD_MARKETPLACE_INVALID_PARAMETER;
		}
		fingerprint = "pawn:" + utils::cryptography::sha256::compute(fingerprint, true);
		std::uint32_t error = BD_MARKETPLACE_STORAGE_ERROR;
		const auto settled = marketplace_store::transact("marketplace:199:" + client_tx, fingerprint,
			[&](marketplace_store::transaction& transaction, std::string& receipt)
			{
				if (!catalog)
				{
					error = BD_SERVICE_NOT_AVAILABLE;
					return false;
				}
				for (const auto& item : items)
				{
					const auto found = catalog->items.find(item.id);
					if (found == catalog->items.end() || !found->second.rule.empty())
					{
						error = BD_MARKETPLACE_MISCONFIGURED;
						return false;
					}
					marketplace_store::inventory_record record;
					error = consume(transaction, item, found->second, user, record);
					// 0x276140 retires failed queue entries on MISCONFIGURED only.
					// Reject the entire batch; never silently discard unpayable rows.
					if (error)
					{
						if (error != BD_MARKETPLACE_STORAGE_ERROR)
						{
							error = BD_MARKETPLACE_MISCONFIGURED;
						}
						return false;
					}
				}
				receipt = "{}";
				return true;
			});
		if (const auto status = error_code(settled.status, error))
		{
			return status;
		}
		const auto state = marketplace_store::get_snapshot();
		if (state.status != marketplace_store::store_status::ready)
		{
			return BD_MARKETPLACE_STORAGE_ERROR;
		}
		std::vector<marketplace_store::inventory_record> changed;
		for (const auto& item : items)
		{
			const auto found = std::ranges::find(state.inventory, item.id, &marketplace_store::inventory_record::item_id);
			auto row = found == state.inventory.end() ? marketplace_store::inventory_record{} : *found;
			row.item_id = item.id;
			changed.push_back(std::move(row));
		}
		// The matched request identifies the rows; transport retries must not
		// overwrite newer absolute inventory or wallet state in the native cache.
		updates = marketplace_inventory::item_updates(changed, user);
		const auto currency = std::ranges::find(state.currencies, 6, &marketplace_store::currency_record::currency_id);
		updates.push_back(balance_push(user, currency == state.currencies.end() ? 0 : currency->value));
		return BD_NO_ERROR;
	}

	marketplace_collection::result convert(const marketplace_collection::request& input,
		const std::uint64_t user, const pawn_catalog::catalog* catalog)
	{
		marketplace_collection::result output;
		if (!user)
		{
			output.error = BD_SERVICE_NOT_AVAILABLE;
			return output;
		}
		std::uint32_t error = BD_MARKETPLACE_STORAGE_ERROR;
		const auto settled = marketplace_store::transact("marketplace:242:" + input.client_tx,
			"pawn:" + std::to_string(user) + ":" + input.rule + ":" + std::to_string(input.quantity),
			[&](marketplace_store::transaction& transaction, std::string& receipt)
			{
				if (!catalog)
				{
					error = BD_SERVICE_NOT_AVAILABLE;
					return false;
				}
				const auto found = std::ranges::find_if(catalog->items,
					[&](const auto& entry) { return !entry.second.rule.empty() && entry.second.rule == input.rule; });
				if (found == catalog->items.end())
				{
					error = BD_SERVICE_NOT_AVAILABLE;
					return false;
				}
				marketplace_store::inventory_record changed;
				error = consume(transaction, {found->first, input.quantity, UINT16_MAX}, found->second, user, changed);
				if (error)
				{
					return false;
				}
				const auto wire = marketplace_collection::balance_update(found->second.currency,
									  transaction.get_currency(found->second.currency), user) +
					marketplace_collection::inventory_update(changed, user);
				receipt = "{\"reply\":\"" + utils::cryptography::base64::encode(wire) + "\"}";
				return true;
			});
		output.error = error_code(settled.status, error);
		if (output.error)
		{
			return output;
		}
		return marketplace_collection::refresh_response(settled.response_json, user);
	}
}
