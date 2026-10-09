#include <std_include.hpp>
#include "purchase.hpp"
#include "cwl.hpp"
#include "store.hpp"
#include "game/demonware/achievement/orders.hpp"
#include "game/types/demonware.hpp"

#include <utils/cryptography.hpp>

namespace demonware::marketplace_purchase
{
	namespace
	{
		using namespace game::demonware;

		bool supported_sku(const std::uint32_t id)
		{
			// Captured type-100 SKUs containing only ordinary rare crates. These
			// have no limiter, unlock, entitlement or conversion requirement.
			// Offer scheduling remains in the stock Quartermaster menu.
			switch (id)
			{
			case 0x40000006: // MP
			case 0x40000019: // Zombies
			case 0x40000068: // MP x20
			case 0x40000069: // MP sale
			case 0x4000006B: // MP x20 sale
			case 0x4000006C: // MP x20 sale
			case 0x4000007B: // MP x5 + Zombies
			case 0x40000082: // Zombies x20 sale
				return true;
			default:
				return false;
			}
		}

		result failure(const std::uint32_t error)
		{
			result value;
			value.error = error;
			return value;
		}

		std::string response(const request& input, const marketplace_sku::price& price,
			const std::uint32_t balance, const std::vector<marketplace_store::inventory_record>& items)
		{
			// SDK 0xA4B770 -> 0xA6C8E0: ClientTx, player assets, granted coupons.
			// The enclosing owner is inherited by currency/inventory/entitlement rows.
			byte_buffer wire;
			wire.write_string(input.client_tx);
			wire.write_uint64(input.player_id);
			wire.write_string("steam");
			wire.write_uint32(1);
			wire.write_ubyte(price.currency);
			wire.write_uint32(balance);
			// Native success 0x27BA60 applies every absolute inventory row.
			wire.write_uint32(static_cast<std::uint32_t>(items.size()));
			for (const auto& item : items)
			{
				wire.write_uint32(item.item_id);
				wire.write_uint32(item.quantity);
				wire.write_uint32(item.item_xp);
				wire.write_blob(item.item_data);
				// Legacy local 0/0 records also represent permanent inventory.
				const auto legacy = !item.expire_date_time && !item.expiry_duration;
				wire.write_uint32(legacy ? UINT32_MAX : item.expire_date_time);
				wire.write_int64(legacy ? INT64_MAX : static_cast<std::int64_t>(item.expiry_duration));
				wire.write_uint16(item.collision_field);
				wire.write_uint32(item.mod_date_time);
				wire.write_uint32(0); // InventoryV3 +200: native default, unused by S2's projection.
			}

			wire.write_uint32(0); // Entitlements (0xA74070).
			wire.write_uint32(0); // Existing coupons.
			wire.write_uint32(0); // Granted coupons.
			return wire.get_buffer();
		}

		std::string refresh_response(const request& input, const std::string& committed)
		{
			// Keep the receipt's affected IDs, even without a catalog after restart.
			// 0x27BA60 applies absolute rows: replaying old quantities would undo
			// a subsequent use/pawn/metadata update in the native cache.
			byte_buffer wire{committed};
			std::string client_tx, account;
			std::uint64_t user{};
			std::uint32_t count{}, ignored{};
			std::uint8_t currency_id{};
			if (!wire.read_string(&client_tx, 24) || client_tx != input.client_tx ||
				!wire.read_uint64(&user) || user != input.player_id ||
				!wire.read_string(&account, 9) || account != "steam" ||
				!wire.read_uint32(&count) || count != 1 || !wire.read_ubyte(&currency_id) ||
				!wire.read_uint32(&ignored) || !wire.read_uint32(&count) || !count || count > marketplace_product::maximum_items)
			{
				return {};
			}

			const auto state = marketplace_store::get_snapshot();
			if (state.status != marketplace_store::store_status::ready)
			{
				return {};
			}

			std::vector<marketplace_store::inventory_record> items;
			for (std::uint32_t i = 0; i < count; ++i)
			{
				std::uint32_t id{};
				std::string data;
				std::int64_t duration{};
				std::uint16_t collision{};
				if (!wire.read_uint32(&id) || !id || !wire.read_uint32(&ignored) ||
					!wire.read_uint32(&ignored) || !wire.read_blob(&data, marketplace_store::max_item_data_length) ||
					!wire.read_uint32(&ignored) || !wire.read_int64(&duration) ||
					!wire.read_uint16(&collision) || !wire.read_uint32(&ignored) ||
					!wire.read_uint32(&ignored) || ignored)
				{
					return {};
				}

				const auto found = std::ranges::find(state.inventory, id, &marketplace_store::inventory_record::item_id);
				auto item = found == state.inventory.end() ? marketplace_store::inventory_record{} : *found;
				item.item_id = id; // Include zero for an exhausted row.
				items.push_back(std::move(item));
			}

			for (auto i = 0; i < 3; ++i)
			{
				if (!wire.read_uint32(&ignored) || ignored)
				{
					return {};
				}
			}

			if (wire.has_more_data())
			{
				return {};
			}

			const auto currency = std::ranges::find(state.currencies, currency_id, &marketplace_store::currency_record::currency_id);
			return response(input, {currency_id, 0}, currency == state.currencies.end() ? 0 : currency->value, items);
		}
	}

	bool parse_request(byte_buffer* buffer, request& output)
	{
		// S2 0x27C640 -> 0x20C9C0 -> SDK 0xA43340 (service 80, task 123).
		// 0xA41770 writes the envelope; 0xA4B3F0 writes each SKU order.
		request parsed;
		std::string context, account;
		std::uint32_t count{}, quantity{}, discount{}, custom_prices{}, coupons{}, source{};
		bool ignore_entitlements{};
		if (!buffer || buffer->remaining_size() > 0x1FFFF + 16 ||
			!buffer->read_string(&context, 15) || context != "s2_steam" ||
			!buffer->read_string(&parsed.client_tx, 24) || parsed.client_tx.empty() ||
			!std::ranges::all_of(parsed.client_tx, [](const unsigned char c) { return c >= 0x21 && c <= 0x7E; }) ||
			!buffer->read_uint64(&parsed.player_id) || !parsed.player_id ||
			!buffer->read_string(&account, 9) || account != "steam" ||
			!buffer->read_uint32(&count) || count != 1 ||
			!buffer->read_uint32(&parsed.sku_id) || !parsed.sku_id ||
			!buffer->read_uint32(&quantity) || quantity != 1 ||
			!buffer->read_uint32(&discount) || discount != 0 ||
			!buffer->read_uint32(&custom_prices) || custom_prices != 0 ||
			!buffer->read_uint32(&coupons) || coupons != 0 ||
			!buffer->read_uint32(&source) || source != 0 ||
			!buffer->read_bool(&ignore_entitlements) || ignore_entitlements ||
			!buffer->has_only_zero_padding(16))
		{
			return false;
		}

		output = std::move(parsed);
		return true;
	}

	void result::serialize(byte_buffer* buffer)
	{
		buffer->write(wire);
	}

	result purchase(const request& input, const std::uint64_t local_user_id,
		const marketplace_sku::result* sku, const marketplace_product::result* product,
		const collection_catalog::catalog* collections)
	{
		if (!local_user_id)
		{
			return failure(BD_SERVICE_NOT_AVAILABLE);
		}

		// 0x276290 completes a non-collection purchase only for error 8003; other
		// errors requeue the SAME transaction. Reject permanent failures with that
		// native terminal error and retain retryable errors for persistence failures.
		constexpr auto terminal_error = BD_MARKETPLACE_INVALID_PARAMETER;
		if (input.player_id != local_user_id)
		{
			return failure(terminal_error);
		}

		const auto* cwl = marketplace_cwl::find(input.sku_id);
		const auto contract = product && product->items.size() == 1 && product->items[0].second == 1 ?
			achievement_orders::contract_for_token(product->items[0].first, static_cast<std::uint64_t>(time(nullptr))) : std::nullopt;
		const auto fingerprint = "purchase:" + std::to_string(input.player_id) + ":" +
			std::to_string(input.sku_id) + ":1";
		std::uint32_t error = BD_MARKETPLACE_STORAGE_ERROR;
		const auto settled = marketplace_store::transact("marketplace:123:" + input.client_tx, fingerprint,
			[&](marketplace_store::transaction& transaction, std::string& receipt)
			{
				if (!sku)
				{
					error = supported_sku(input.sku_id) ? BD_SERVICE_NOT_AVAILABLE : terminal_error;
					return false;
				}

				const auto collection = sku->sku_type == marketplace_sku::collection_sku_type;
				// Type 150 bypasses the native product cache (0x276580/0x27C640).
				// The captured SKU is the collectible GUID; no rarity-derived price.
				if (collection && !collections)
				{
					error = BD_SERVICE_NOT_AVAILABLE;
					return false;
				}

				if (sku->sku_id != input.sku_id || sku->sold_out ||
					sku->maximum_quantity != UINT32_MAX || sku->prices.size() != 1 ||
					sku->prices[0].currency != (collection || contract || cwl ? 6 : 2) || !sku->prices[0].value ||
					sku->field_40 != 1 || sku->field_106 || sku->field_107 != 1 ||
					sku->field_108 || sku->field_112 || sku->field_116 || sku->field_120 != 2 ||
					sku->field_268 || sku->field_272 || sku->field_276)
				{
					error = terminal_error;
					return false;
				}

				std::vector<marketplace_product::pair> contents;
				if (collection)
				{
					if (sku->field_36 != input.sku_id || !sku->sku_data.empty() ||
						!sku->promotional_text.empty() || !collections->purchasable_items.contains(input.sku_id))
					{
						error = terminal_error;
						return false;
					}

					contents.emplace_back(input.sku_id, 1);
				}
				else
				{
					if ((!supported_sku(input.sku_id) && !contract && !cwl) || sku->sku_type != marketplace_sku::quartermaster_sku_type)
					{
						error = terminal_error;
						return false;
					}

					if (!product)
					{
						error = BD_SERVICE_NOT_AVAILABLE;
						return false;
					}

					if (product->product_id != sku->field_36 || product->items.empty() || product->items.size() > (cwl ? cwl->items.size() : 2) ||
						product->field_20 || product->field_24 || !product->blob_3.empty() ||
						!product->pairs_1.empty() || !product->pairs_2.empty())
					{
						error = terminal_error;
						return false;
					}

					contents = product->items;
				}

				const auto& price = sku->prices[0];
				if (cwl)
				{
					const auto owned = transaction.get_inventory(cwl->items[0]);
					if (price.value != marketplace_cwl::price || contents.size() != cwl->items.size() ||
						(owned && owned->quantity))
					{
						error = terminal_error;
						return false;
					}

					for (std::size_t i = 0; i < contents.size(); ++i)
					{
						if (contents[i].first != cwl->items[i] || contents[i].second != 1)
						{
							error = terminal_error;
							return false;
						}
					}
				}

				if (contract)
				{
					// The restored retail costItemID must be the product's sole unit.
					// Check active ownership under the economy lock, including races
					// with activation; an active contract cannot buy another token.
					const auto& state = transaction.get_achievement_state();
					rapidjson::Document achievements;
					achievements.Parse(state.data(), state.size());
					if (achievements.HasParseError() || !achievements.IsObject() ||
						!achievements.HasMember("achievements") || !achievements["achievements"].IsArray())
					{
						return false;
					}

					std::uint32_t active{};
					for (const auto& achievement : achievements["achievements"].GetArray())
					{
						if (!achievement.IsObject())
						{
							return false;
						}

						if (!achievement.HasMember("kind") || !achievement["kind"].IsInt() || achievement["kind"].GetInt() != contract->achievement.kind)
						{
							continue;
						}

						if (!achievement.HasMember("name") || !achievement["name"].IsString() ||
							!achievement.HasMember("status") || !achievement["status"].IsString())
						{
							return false;
						}

						const std::string_view status = achievement["status"].GetString();
						if (status == "inProgress" || status == "claimable")
						{
							++active;
						}

						const auto repeat = contract->period_start && (status == "finished" || status == "inactive") &&
							achievement.HasMember("activationTimestamp") && achievement["activationTimestamp"].IsUint64() &&
							achievement["activationTimestamp"].GetUint64() < contract->period_start;
						if (achievement["name"].GetString() == contract->achievement.name && !repeat)
						{
							error = terminal_error;
							return false;
						}
					}

					if (active >= contract->activation_limit)
					{
						error = terminal_error;
						return false;
					}
				}

				const auto owned_token = contract ? transaction.get_inventory(contract->cost_item_id) : std::nullopt;
				if ((!owned_token || !owned_token->quantity) && transaction.get_currency(price.currency) < price.value)
				{
					error = collection ? BD_MARKETPLACE_INSUFFICIENT_FUNDS_ERROR : terminal_error;
					return false;
				}

				const auto now = std::time(nullptr);
				if (now <= 0 || static_cast<std::uint64_t>(now) >= UINT32_MAX)
				{
					return false;
				}

				std::vector<marketplace_store::inventory_record> items;
				for (const auto& [item_id, quantity] : contents)
				{
					if ((!collection && !contract && !cwl && item_id != 2 && item_id != 6) || !quantity || quantity > INT32_MAX ||
						std::ranges::any_of(items, [item_id](const auto& item) { return item.item_id == item_id; }))
					{
						error = terminal_error;
						return false;
					}

					auto item = transaction.get_inventory(item_id).value_or(marketplace_store::inventory_record{});
					if ((item.player_id && item.player_id != local_user_id) ||
						(!item.account_type.empty() && item.account_type != "steam") || item.collision_field ||
						((item.expire_date_time || item.expiry_duration) &&
							(item.expire_date_time != UINT32_MAX || item.expiry_duration != INT64_MAX)) ||
						item.quantity > INT32_MAX - quantity)
					{
						error = terminal_error;
						return false;
					}

					if (collection && item.quantity)
					{
						error = BD_MARKETPLACE_ITEM_MULTIPLE_PURCHASE_ERROR;
						return false;
					}

					if (!item.item_id)
					{
						item.item_id = item_id;
						item.player_id = local_user_id;
						item.account_type = "steam";
						item.expire_date_time = UINT32_MAX;
						item.expiry_duration = INT64_MAX;
					}

					if (contract && item.quantity > 1)
					{
						error = terminal_error;
						return false;
					}

					if ((!contract && !cwl) || !item.quantity)
					{
						item.quantity += quantity;
					}
					item.mod_date_time = static_cast<std::uint32_t>(now);
					const auto edited = transaction.set_inventory(item);
					if (edited != marketplace_store::edit_result::updated &&
						!((contract || cwl) && edited == marketplace_store::edit_result::unchanged))
					{
						return false;
					}

					items.push_back(std::move(item));
				}

				if ((!owned_token || !owned_token->quantity) && transaction.consume_currency(price.currency, price.value) != marketplace_store::edit_result::updated)
				{
					return false;
				}

				// Persist the exact native result with the mutation. A lost response can
				// be retried after a restart without another debit or another grant.
				const auto encoded = utils::cryptography::base64::encode(response(input, price,
					transaction.get_currency(price.currency), items));
				receipt = "{\"reply\":\"" + encoded + "\"}";
				return true;
			});

		using enum marketplace_store::transaction_status;
		if (settled.status != committed && settled.status != replayed)
		{
			if (settled.status == client_tx_conflict || settled.status == invalid_argument)
			{
				return failure(terminal_error);
			}

			return failure(settled.status == marketplace_store::transaction_status::rejected ? error : BD_MARKETPLACE_STORAGE_ERROR);
		}

		rapidjson::Document receipt;
		receipt.Parse(settled.response_json.data(), settled.response_json.size());
		if (receipt.HasParseError() || !receipt.IsObject() || !receipt.HasMember("reply") ||
			!receipt["reply"].IsString())
		{
			return failure(BD_MARKETPLACE_STORAGE_ERROR);
		}

		result output;
		output.wire = refresh_response(input, utils::cryptography::base64::decode(receipt["reply"].GetString()));
		if (output.wire.empty())
		{
			return failure(BD_MARKETPLACE_STORAGE_ERROR);
		}

		return output;
	}
}
