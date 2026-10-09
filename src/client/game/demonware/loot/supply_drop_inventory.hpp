#pragma once

#include "game/demonware/marketplace/store.hpp"
#include <limits>

namespace demonware::supply_drop_inventory
{
	bool refresh_replay(std::string& response, std::uint64_t user);

	inline bool can_stack(const marketplace_store::inventory_record& item)
	{
		// The verified duplicate queues consume permanent, collision-zero rows.
		// Do not turn a rental/expired row into permanent ownership by stacking.
		return !item.collision_field && item.quantity && item.quantity < INT32_MAX &&
			((!item.expire_date_time && !item.expiry_duration) ||
				(item.expire_date_time == UINT32_MAX && item.expiry_duration == INT64_MAX));
	}

	inline bool grant(marketplace_store::transaction& transaction, const std::uint32_t item_id,
		const std::uint64_t user, const std::uint32_t time,
		marketplace_store::inventory_record& result)
	{
		const auto owned = transaction.get_inventory(item_id);
		if (owned && (!can_stack(*owned) || (owned->player_id && owned->player_id != user) || (!owned->account_type.empty() && owned->account_type != "steam")))
		{
			return false;
		}
		result = owned.value_or(marketplace_store::inventory_record{});
		result.item_id = item_id;
		result.player_id = user;
		result.account_type = "steam";
		++result.quantity;
		result.mod_date_time = time;
		// Preserve metadata and return the absolute post-grant quantity. The
		// native DetailedInventory parser (0x27C1D0) replaces, rather than adds.
		return transaction.set_inventory(result) == marketplace_store::edit_result::updated;
	}
}
