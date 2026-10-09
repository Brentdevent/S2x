#include <std_include.hpp>
#include "collection.hpp"
#include "store.hpp"
#include "inventory_expiry.hpp"
#include "game/demonware/struct_buffer_reader.hpp"
#include "game/types/demonware.hpp"

#include <utils/cryptography.hpp>

namespace demonware::marketplace_collection
{
	namespace
	{
		using namespace game::demonware;

		result failure(const std::uint32_t error)
		{
			result value;
			value.error = error;
			return value;
		}

		void integer(std::string& bytes, std::uint64_t value)
		{
			do
			{
				bytes += static_cast<char>((value & 127) | (value > 127 ? 128 : 0));
				value >>= 7;
			} while (value);
		}

		void field(std::string& bytes, const unsigned tag, const std::uint64_t value)
		{
			integer(bytes, tag << 3);
			integer(bytes, value);
		}

		void field(std::string& bytes, const unsigned tag, const std::string& value)
		{
			integer(bytes, (tag << 3) | 2);
			integer(bytes, value.size());
			bytes += value;
		}

		bool permanent(const marketplace_store::inventory_record& item, const std::uint64_t user)
		{
			return (!item.player_id || item.player_id == user) &&
				(item.account_type.empty() || item.account_type == "steam") && !item.collision_field &&
				((!item.expire_date_time && !item.expiry_duration) ||
					(item.expire_date_time == UINT32_MAX && item.expiry_duration == INT64_MAX));
		}

	}

	std::string inventory_update(const marketplace_store::inventory_record& item, const std::uint64_t user)
	{
		// SDK 0xA4C850: tags 1-3 (server transaction/time/applied-rule metadata)
		// are optional. Their backend values are unknown and S2 does not use
		// them. Emit only the absolute inventory update used by 0x27A4C0.
		std::string account, row, bytes;
		field(account, 1, user);
		field(account, 2, std::string{"steam"});
		field(row, 1, account);
		field(row, 2, std::string{"s2_steam"});
		field(row, 3, item.item_id);
		field(row, 4, item.quantity);
		field(row, 5, item.item_xp);
		field(row, 6, item.item_data);
		field(row, 7, 0); // Non-rental inventory.
		const auto expiry = inventory_expiry::for_task165(item.expire_date_time, item.expiry_duration);
		field(row, 8, expiry.date);
		field(row, 10, item.mod_date_time);
		field(row, 11, item.collision_field);
		// 0xA6DCF0 -> 0xA64370: signed duration uses protobuf ZigZag.
		field(row, 12, static_cast<std::uint64_t>(expiry.duration) << 1);
		field(bytes, 5, row);
		return bytes;
	}

	std::string balance_update(const std::uint8_t currency, const std::uint32_t balance, const std::uint64_t user)
	{
		// 0xA6DB90: account, context, currency, absolute balance. The remaining
		// optional fields retain SDK defaults; 0x27A4C0 applies this balance.
		std::string account, row, bytes;
		field(account, 1, user);
		field(account, 2, std::string{"steam"});
		field(row, 1, account);
		field(row, 2, std::string{"s2_steam"});
		field(row, 3, currency);
		field(row, 4, balance);
		field(bytes, 4, row);
		return bytes;
	}

	result refresh_response(const std::string& receipt_json, const std::uint64_t user)
	{
		rapidjson::Document receipt;
		receipt.Parse(receipt_json.data(), receipt_json.size());
		if (receipt.HasParseError() || !receipt.IsObject() || !receipt.HasMember("reply") ||
			!receipt["reply"].IsString())
		{
			return failure(BD_MARKETPLACE_STORAGE_ERROR);
		}

		const auto committed = utils::cryptography::base64::decode(receipt["reply"].GetString());
		const auto state = marketplace_store::get_snapshot();
		if (committed.empty() || state.status != marketplace_store::store_status::ready)
		{
			return failure(BD_MARKETPLACE_STORAGE_ERROR);
		}

		// Our committed Task 242 result identifies the affected currency/item.
		// Preserve those identities, but project current absolute state just as
		// Task 165 does. No catalog, second conversion or receipt mutation is needed.
		struct_buffer_reader reader{committed};
		result output;
		std::unordered_set<std::uint64_t> seen;
		while (!reader.empty())
		{
			std::uint32_t tag{};
			std::uint8_t type{};
			std::string_view row;
			if (!reader.read_tag(tag, type) || (tag != 4 && tag != 5) || type != 2 ||
				!reader.read_length_delimited(row))
			{
				return failure(BD_MARKETPLACE_STORAGE_ERROR);
			}

			struct_buffer_reader fields{row};
			std::optional<std::uint32_t> id;
			while (!fields.empty())
			{
				std::uint32_t field{};
				if (!fields.read_tag(field, type))
				{
					return failure(BD_MARKETPLACE_STORAGE_ERROR);
				}

				if (field == 3)
				{
					std::uint64_t value{};
					if (id || type != 0 || !fields.read_varint(value) || !value ||
						value > (tag == 4 ? UINT8_MAX : UINT32_MAX))
					{
						return failure(BD_MARKETPLACE_STORAGE_ERROR);
					}

					id = static_cast<std::uint32_t>(value);
				}
				else if (!fields.skip_field(type))
				{
					return failure(BD_MARKETPLACE_STORAGE_ERROR);
				}
			}

			if (!id || !seen.insert((std::uint64_t{tag} << 32) | *id).second)
			{
				return failure(BD_MARKETPLACE_STORAGE_ERROR);
			}

			if (tag == 4)
			{
				const auto currency = std::ranges::find(state.currencies, *id, &marketplace_store::currency_record::currency_id);
				output.wire += balance_update(static_cast<std::uint8_t>(*id),
					currency == state.currencies.end() ? 0 : currency->value, user);
			}
			else
			{
				const auto found = std::ranges::find(state.inventory, *id, &marketplace_store::inventory_record::item_id);
				auto item = found == state.inventory.end() ? marketplace_store::inventory_record{} : *found;
				item.item_id = *id;
				output.wire += inventory_update(item, user);
			}
		}

		return output;
	}

	bool parse_request(byte_buffer* buffer, request& output)
	{
		// SDK 0xA4CBA0 serializes exactly these four fields, in order. All
		// native string lengths fit one byte. Quantity is a uint32 varint;
		// each supported rule validates its own consumption semantics.
		std::string bytes;
		if (!buffer || !buffer->read_struct(&bytes, 128) || !buffer->has_only_zero_padding(16))
		{
			return false;
		}

		std::size_t cursor{};
		const auto string = [&](const unsigned char key, const std::size_t maximum, std::string& value)
		{
			if (bytes.size() - cursor < 2 || static_cast<unsigned char>(bytes[cursor++]) != key)
			{
				return false;
			}

			const auto length = static_cast<unsigned char>(bytes[cursor++]);
			if (!length || length > maximum || length > bytes.size() - cursor)
			{
				return false;
			}

			value.assign(bytes, cursor, length);
			cursor += length;
			return std::ranges::all_of(value, [](const unsigned char c) { return c >= 0x21 && c <= 0x7E; });
		};
		request parsed;
		std::string context;
		if (!string(10, 15, context) || context != "s2_steam" ||
			!string(18, 36, parsed.rule) || parsed.rule.size() != 36 ||
			!string(26, 24, parsed.client_tx) || bytes.size() - cursor < 2 ||
			bytes[cursor++] != 32)
		{
			return false;
		}

		parsed.quantity = 0;
		unsigned shift{};
		for (;; shift += 7)
		{
			if (cursor == bytes.size() || shift > 28)
			{
				return false;
			}

			const auto value = static_cast<unsigned char>(bytes[cursor++]);
			if (shift == 28 && value > 15)
			{
				return false;
			}

			parsed.quantity |= static_cast<std::uint32_t>(value & 127) << shift;
			if (!(value & 128))
			{
				if (shift && !value)
				{
					return false;
				}
				break;
			}
		}

		if (!parsed.quantity || cursor != bytes.size())
		{
			return false;
		}

		output = std::move(parsed);
		return true;
	}

	void result::serialize(byte_buffer* buffer)
	{
		buffer->write_struct(wire.data(), static_cast<int>(wire.size()));
	}

	result redeem(const request& input, const std::uint64_t user_id, const collection_catalog::catalog* catalog)
	{
		if (!user_id)
		{
			return failure(BD_SERVICE_NOT_AVAILABLE);
		}

		if (input.quantity != 1)
		{
			return failure(BD_MARKETPLACE_INVALID_PARAMETER);
		}

		std::uint32_t error = BD_MARKETPLACE_STORAGE_ERROR;
		const auto settled = marketplace_store::transact("marketplace:242:" + input.client_tx,
			"collection:" + std::to_string(user_id) + ":" + input.rule + ":1",
			[&](marketplace_store::transaction& transaction, std::string& receipt)
			{
				if (!catalog)
				{
					error = BD_SERVICE_NOT_AVAILABLE;
					return false;
				}

				const auto found = std::ranges::find(catalog->collections, input.rule, &collection_catalog::collection::rule);
				if (found == catalog->collections.end())
				{
					error = BD_SERVICE_NOT_AVAILABLE;
					return false;
				}

				auto reward = transaction.get_inventory(found->reward).value_or(marketplace_store::inventory_record{});
				if (!permanent(reward, user_id))
				{
					error = BD_MARKETPLACE_INVALID_PARAMETER;
					return false;
				}

				// 0x276780 permits an already-owned reward to be redeemed again.
				// Ownership is the completion state: never increment it a second time.
				if (!reward.quantity)
				{
					for (const auto id : found->items)
					{
						const auto item = transaction.get_inventory(id);
						if (!item || !item->quantity || !permanent(*item, user_id))
						{
							error = BD_MARKETPLACE_INSUFFICIENT_ITEM_QUANTITY;
							return false;
						}
					}

					const auto now = std::time(nullptr);
					if (now <= 0 || static_cast<std::uint64_t>(now) >= UINT32_MAX)
					{
						return false;
					}

					reward.item_id = found->reward;
					reward.player_id = user_id;
					reward.account_type = "steam";
					reward.quantity = 1;
					reward.expire_date_time = UINT32_MAX;
					reward.expiry_duration = INT64_MAX;
					reward.mod_date_time = static_cast<std::uint32_t>(now);
					if (transaction.set_inventory(reward) != marketplace_store::edit_result::updated)
					{
						return false;
					}
				}

				// Required pieces remain owned/equippable. The stock collection
				// progress path (0x274A70) continues counting them after redemption.
				receipt = "{\"reply\":\"" + utils::cryptography::base64::encode(inventory_update(reward, user_id)) + "\"}";
				return true;
			});

		using enum marketplace_store::transaction_status;
		if (settled.status != committed && settled.status != replayed)
		{
			if (settled.status == client_tx_conflict)
			{
				return failure(BD_MARKETPLACE_IDEMPOTENT_REQUEST_COLLISION);
			}

			if (settled.status == invalid_argument)
			{
				return failure(BD_MARKETPLACE_INVALID_PARAMETER);
			}

			return failure(settled.status == rejected ? error : BD_MARKETPLACE_STORAGE_ERROR);
		}

		return refresh_response(settled.response_json, user_id);
	}
}
