#include <std_include.hpp>
#include "inventory.hpp"
#include "game/demonware/byte_buffer.hpp"
#include "store.hpp"
#include "inventory_expiry.hpp"
#include "game/types/demonware.hpp"

#include <utils/cryptography.hpp>

namespace demonware::marketplace_inventory
{
	namespace
	{
		using namespace game::demonware;

		bool read_header(byte_buffer* buffer, std::string& client_tx)
		{
			// The native SDK can append a complete zero block to aligned requests.
			std::string context;
			return buffer && buffer->remaining_size() <= 0x1FFFF + 16 &&
				buffer->read_string(&context, 15) && context == "s2_steam" &&
				buffer->read_string(&client_tx, 24) && !client_tx.empty() &&
				std::ranges::all_of(client_tx, [](const unsigned char value)
				{
					return value >= 0x21 && value <= 0x7E;
				});
		}

		std::uint32_t transaction_error(const marketplace_store::transaction_status status,
			const std::uint32_t rejected_error)
		{
			using enum marketplace_store::transaction_status;
			switch (status)
			{
			case committed:
			case replayed: return BD_NO_ERROR;
			case client_tx_conflict: return BD_MARKETPLACE_IDEMPOTENT_REQUEST_COLLISION;
			case invalid_argument: return BD_PARAM_PARSE_ERROR;
			case rejected: return rejected_error;
			default: return BD_MARKETPLACE_STORAGE_ERROR;
			}
		}
	}

	std::vector<std::string> item_updates(const std::vector<marketplace_store::inventory_record>& changed,
		const std::uint64_t local_user_id)
	{
		// 0xA34740 accepts six inherited-owner rows per 0x31 push.
		std::vector<std::string> result;
		for (std::size_t first = 0; first < changed.size(); first += 6)
		{
			byte_buffer push;
			push.write_uint32(BD_MARKETPLACE_ITEMS_UPDATED);
			push.write_uint64(local_user_id);
			push.write_string("steam");
			push.write_string("s2_steam");
			const auto last = std::min(first + 6, changed.size());
			push.write_uint32(static_cast<std::uint32_t>(last - first));
			for (auto index = first; index < last; ++index)
			{
				const auto& item = changed[index];
				const auto expiry = inventory_expiry::to_wire(item.expire_date_time, item.expiry_duration);
				push.write_uint32(item.item_id);
				push.write_uint32(item.quantity);
				push.write_uint32(item.item_xp);
				push.write_blob(item.item_data);
				push.write_uint32(expiry.date);
				push.write_int64(expiry.duration);
				push.write_uint16(item.collision_field);
				push.write_uint32(item.mod_date_time);
			}

			result.push_back(push.get_buffer());
		}

		return result;
	}

	std::uint32_t consume(byte_buffer* buffer, const std::uint64_t local_user_id,
		std::vector<std::string>& updates)
	{
		updates.clear();
		if (!local_user_id)
		{
			return BD_SERVICE_NOT_AVAILABLE;
		}

		// S2 0x278480 holds ten IDs and ten quantities. SDK 0xA41B20 writes
		// context, ClientTx, uint32 count/IDs, then uint32 count/quantities;
		// the native adapter 0x20BE10 supplies no result object.
		std::string client_tx;
		std::uint32_t count{}, quantity_count{};
		std::array<std::uint32_t, 10> ids{}, quantities{};
		if (!read_header(buffer, client_tx) || !buffer->read_uint32(&count) ||
			!count || count > ids.size())
		{
			return BD_PARAM_PARSE_ERROR;
		}

		for (std::uint32_t index = 0; index < count; ++index)
		{
			if (!buffer->read_uint32(&ids[index]) || !ids[index] ||
				std::find(ids.begin(), ids.begin() + index, ids[index]) != ids.begin() + index)
			{
				return BD_PARAM_PARSE_ERROR;
			}
		}

		if (!buffer->read_uint32(&quantity_count) || quantity_count != count)
		{
			return BD_PARAM_PARSE_ERROR;
		}

		std::string fingerprint{"consume:"};
		for (std::uint32_t index = 0; index < count; ++index)
		{
			if (!buffer->read_uint32(&quantities[index]) || !quantities[index])
			{
				return BD_PARAM_PARSE_ERROR;
			}

			fingerprint += std::to_string(ids[index]) + ":" +
				std::to_string(quantities[index]) + ";";
		}

		if (!buffer->has_only_zero_padding(16))
		{
			return BD_PARAM_PARSE_ERROR;
		}

		const auto current_time = std::time(nullptr);
		if (current_time <= 0 || static_cast<std::uint64_t>(current_time) >=
			std::numeric_limits<std::uint32_t>::max())
		{
			return BD_SERVICE_NOT_AVAILABLE;
		}

		const auto now = static_cast<std::uint32_t>(current_time);
		std::uint32_t error = BD_MARKETPLACE_STORAGE_ERROR;
		const auto result = marketplace_store::transact("marketplace:96:" + client_tx, fingerprint,
			[&](marketplace_store::transaction& transaction, std::string& receipt)
			{
				for (std::uint32_t index = 0; index < count; ++index)
				{
					auto record = transaction.get_inventory(ids[index]);
					if (!record || record->quantity < quantities[index])
					{
						error = BD_MARKETPLACE_INSUFFICIENT_ITEM_QUANTITY;
						return false;
					}

					if (!marketplace_store::owns_inventory(*record, local_user_id))
					{
						return false;
					}

					// Preserve legacy 0/0 permanent records; otherwise use S2's
					// verified expiry predicate (0x279650), without inventing a timer.
					if ((record->expire_date_time || record->expiry_duration) &&
						((record->expire_date_time != UINT32_MAX && now >= record->expire_date_time) ||
							!record->expiry_duration))
					{
						error = BD_MARKETPLACE_ITEMS_EXPIRED;
						return false;
					}

					record->quantity -= quantities[index];
					record->mod_date_time = now;
					if (transaction.set_inventory(*record) != marketplace_store::edit_result::updated)
					{
						return false;
					}
				}

				// Task 96 has no result object; the ledger records the debit, while
				// the inventory push below always projects current absolute stock.
				receipt = "{}";
				return true;
			});

		const auto status = transaction_error(result.status, error);
		if (status)
		{
			return status;
		}

		const auto state = marketplace_store::get_snapshot();
		if (state.status != marketplace_store::store_status::ready)
		{
			return BD_MARKETPLACE_STORAGE_ERROR;
		}

		std::vector<marketplace_store::inventory_record> changed;
		for (std::uint32_t index = 0; index < count; ++index)
		{
			const auto found = std::ranges::find(state.inventory, ids[index],
				&marketplace_store::inventory_record::item_id);
			marketplace_store::inventory_record row;
			if (found != state.inventory.end())
			{
				row = *found;
			}
			else
			{
				row.item_id = ids[index]; // Zero quantity removes an exhausted native row.
			}
			changed.push_back(std::move(row));
		}

		// Replaying an older debit must not restore stock or metadata from before
		// a later use/grant. The matched request supplies even the deleted IDs.
		updates = item_updates(changed, local_user_id);
		return BD_NO_ERROR;
	}

	std::uint32_t put_item_data(byte_buffer* buffer, const std::uint64_t local_user_id)
	{
		if (!local_user_id)
		{
			return BD_SERVICE_NOT_AVAILABLE;
		}

		std::string client_tx;
		std::uint32_t count{};
		if (!read_header(buffer, client_tx) || !buffer->read_uint32(&count) || !count || count > 30)
		{
			return BD_PARAM_PARSE_ERROR;
		}

		struct item_data_update
		{
			std::uint32_t id{};
			std::string data;
			std::string account_type;
			std::uint16_t collision{};
		};
		std::vector<item_data_update> updates(count);
		for (std::uint32_t index = 0; index < count; ++index)
		{
			auto& update = updates[index];
			std::uint64_t user_id{};
			// Native 0x27D6B0 sends at most 30 records through SDK 0xA434F0.
			// Serializer 0xA4AE60: owner/account, GUID, <=64 opaque bytes, collision.
			if (!buffer->read_uint64(&user_id) || user_id != local_user_id ||
				!buffer->read_string(&update.account_type, 9) ||
				(!update.account_type.empty() && update.account_type != "steam") ||
				!buffer->read_uint32(&update.id) || !update.id ||
				!buffer->read_blob(&update.data, marketplace_store::max_item_data_length) ||
				!buffer->read_uint16(&update.collision) ||
				std::any_of(updates.begin(), updates.begin() + index,
					[&](const auto& prior) { return prior.id == update.id; }))
			{
				return BD_PARAM_PARSE_ERROR;
			}
		}

		if (!buffer->has_only_zero_padding(16))
		{
			return BD_PARAM_PARSE_ERROR;
		}

		// Rebuild the same validated request without transport padding, replacing
		// only its owner fields. Retain account, order, data and collision values.
		const auto hash_request = [&](const std::uint64_t owner)
		{
			byte_buffer request;
			request.set_use_data_types(buffer->is_using_data_types());
			request.write_string("s2_steam");
			request.write_string(client_tx);
			request.write_uint32(count);

			for (const auto& update : updates)
			{
				request.write_uint64(owner);
				request.write_string(update.account_type);
				request.write_uint32(update.id);
				request.write_blob(update.data);
				request.write_uint16(update.collision);
			}

			return utils::cryptography::sha256::compute(request.get_buffer(), true);
		};

		std::uint32_t error = BD_MARKETPLACE_STORAGE_ERROR;
		const auto result = marketplace_store::transact_profile_hashed("marketplace:168:" + client_tx,
			local_user_id, "item-data:", hash_request,
			[&](marketplace_store::transaction& transaction, std::string& receipt)
			{
				for (const auto& update : updates)
				{
					auto record = transaction.get_inventory(update.id);
					if (!record || !marketplace_store::owns_inventory(*record, local_user_id))
					{
						error = BD_MARKETPLACE_RESOURCE_NOT_FOUND;
						return false;
					}

					if (record->collision_field != update.collision)
					{
						error = BD_MARKETPLACE_RESOURCE_CONFLICT;
						return false;
					}

					record->item_data = update.data;
					const auto edited = transaction.set_inventory(*record);
					if (edited != marketplace_store::edit_result::updated &&
						edited != marketplace_store::edit_result::unchanged)
					{
						return false;
					}
				}

				receipt = "{}";
				return true;
			});

		// S2 binds no audit-log result. Its success callback 0x27BED0 clears
		// in-flight metadata only after this persistent update has succeeded.
		return transaction_error(result.status, error);
	}
}
