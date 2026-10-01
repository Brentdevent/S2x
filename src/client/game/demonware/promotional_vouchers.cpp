#include <std_include.hpp>
#include "promotional_vouchers.hpp"
#include "collection_catalog.hpp"
#include "marketplace_store.hpp"
#include "game/types/demonware.hpp"

#include <utils/cryptography.hpp>
#include <utils/string.hpp>

namespace demonware::promotional_vouchers
{
	namespace
	{
		using namespace game::demonware;
		struct voucher
		{
			std::uint32_t id;
			std::vector<std::uint32_t> rewards;
			std::string rule;
		};

		const auto& vouchers()
		{
			// Shipped StatsTable voucher/cosmetic identities. Original entitlement
			// eligibility and backend conversions are gone: S2x delivers these five
			// vouchers once to every local profile. Do not repurpose their IDs or
			// change these reward sets; the permanent receipts identify this policy.
			static const std::array definitions{
				voucher{0x80006E, {0x2000003, 0x2400222, 0x6632101}, {}}, // Beta Pack
				voucher{0x80009E, {0x2400267}, {}}, // Ambassador Hero
				voucher{0x8000A0, {0x2400268}, {}}, // Ambassador Champion
				voucher{0x8000A2, {0x2400269}, {}}, // Ambassador Legend
				voucher{0x8000AD, {0x2000005}, {}} // Ambassador emblem
			};
			static const auto result = []
			{
				auto rows = definitions;
				for (auto& row : rows)
				{
					// Native Inventory_RedeemVoucherItem -> 0x2768B0 -> 0x278180.
					row.rule = collection_catalog::rule_id(utils::string::va("voucher_%X", row.id));
				}
				return rows;
			}();
			return result;
		}

		bool permanent(const marketplace_store::inventory_record& row, const std::uint64_t user)
		{
			return (!row.player_id || row.player_id == user) &&
				(row.account_type.empty() || row.account_type == "steam") && !row.collision_field &&
				((!row.expire_date_time && !row.expiry_duration) ||
					(row.expire_date_time == UINT32_MAX && row.expiry_duration == INT64_MAX));
		}

		bool grant(marketplace_store::transaction& state, const std::uint32_t id,
			const std::uint64_t user, const std::uint32_t now, const bool delivery)
		{
			auto row = state.get_inventory(id).value_or(marketplace_store::inventory_record{});
			if (!permanent(row, user) || row.quantity >= INT32_MAX)
			{
				return false;
			}
			// A manually supplied voucher already represents the pending delivery.
			// Cosmetic ownership/unlock-all never serves as the one-time receipt.
			if (delivery && row.quantity)
			{
				return true;
			}
			row.item_id = id;
			row.player_id = user;
			row.account_type = "steam";
			++row.quantity;
			row.expire_date_time = UINT32_MAX;
			row.expiry_duration = INT64_MAX;
			row.mod_date_time = now;
			return state.set_inventory(row) == marketplace_store::edit_result::updated;
		}

		std::uint32_t error(const marketplace_store::transaction_status status, const std::uint32_t rejected_error)
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
			return status == marketplace_store::transaction_status::rejected ? rejected_error : BD_MARKETPLACE_STORAGE_ERROR;
		}
	}

	bool deliver(const std::uint64_t user)
	{
		if (!user)
		{
			return false;
		}
		const auto saved = marketplace_store::transact("promotions:beta-ambassador:delivery",
			std::to_string(user), [&](marketplace_store::transaction& state, std::string& receipt)
			{
				const auto now = std::time(nullptr);
				if (now <= 0 || static_cast<std::uint64_t>(now) >= UINT32_MAX)
				{
					return false;
				}
				for (const auto& row : vouchers())
				{
					if (!grant(state, row.id, user, static_cast<std::uint32_t>(now), true))
					{
						return false;
					}
				}
				receipt = "{}";
				return true;
			});
		return !error(saved.status, BD_MARKETPLACE_STORAGE_ERROR);
	}

	std::optional<marketplace_collection::result> redeem(const marketplace_collection::request& input,
		const std::uint64_t user)
	{
		const auto& definitions = vouchers();
		const auto found = std::ranges::find(definitions, input.rule, &voucher::rule);
		if (found == definitions.end())
		{
			return {};
		}
		marketplace_collection::result output;
		if (!user)
		{
			output.error = BD_SERVICE_NOT_AVAILABLE;
			return output;
		}
		if (input.quantity != 1)
		{
			output.error = BD_MARKETPLACE_INVALID_PARAMETER;
			return output;
		}
		std::uint32_t rejected = BD_MARKETPLACE_STORAGE_ERROR;
		// Claim identity is the promotion, not the native UI's newly generated
		// ClientTx. Reopening/restarting/retrying cannot redeem it a second time.
		const auto saved = marketplace_store::transact("promotions:claim:" + std::to_string(found->id),
			std::to_string(user), [&](marketplace_store::transaction& state, std::string& receipt)
			{
				auto item = state.get_inventory(found->id);
				if (!item || !item->quantity)
				{
					rejected = BD_MARKETPLACE_INSUFFICIENT_ITEM_QUANTITY;
					return false;
				}
				if (!permanent(*item, user))
				{
					rejected = BD_MARKETPLACE_INVALID_PARAMETER;
					return false;
				}
				const auto now = std::time(nullptr);
				if (now <= 0 || static_cast<std::uint64_t>(now) >= UINT32_MAX)
				{
					return false;
				}
				// One-time mail is cleared completely, including manually added copies.
				item->quantity = 0;
				item->mod_date_time = static_cast<std::uint32_t>(now);
				if (state.set_inventory(*item) != marketplace_store::edit_result::updated)
				{
					return false;
				}
				auto wire = marketplace_collection::inventory_update(*item, user);
				for (const auto id : found->rewards)
				{
					if (!grant(state, id, user, static_cast<std::uint32_t>(now), false))
					{
						return false;
					}
					wire += marketplace_collection::inventory_update(*state.get_inventory(id), user);
				}
				receipt = "{\"reply\":\"" + utils::cryptography::base64::encode(wire) + "\"}";
				return true;
			});
		output.error = error(saved.status, rejected);
		if (output.error)
		{
			return output;
		}
		// Task 242's native completion applies these absolute rows, refreshes
		// unlocks and voucher counts, then signals the stock Post menu to clear.
		return marketplace_collection::refresh_response(saved.response_json, user);
	}
}
