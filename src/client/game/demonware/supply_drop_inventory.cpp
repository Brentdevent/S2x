#include <std_include.hpp>
#include "supply_drop_inventory.hpp"

namespace demonware::supply_drop_inventory
{
	bool refresh_replay(std::string& response, const std::uint64_t user)
	{
		rapidjson::Document document;
		document.Parse(response.data(), response.size());
		if (document.HasParseError() || !document.IsObject() ||
			!document.HasMember("DetailedInventory") || !document["DetailedInventory"].IsArray())
		{
			return false;
		}
		const auto current = marketplace_store::get_snapshot();
		if (current.status != marketplace_store::store_status::ready)
		{
			return false;
		}
		for (auto& row : document["DetailedInventory"].GetArray())
		{
			if (!row.IsObject() || !row.HasMember("item_id") || !row["item_id"].IsUint() ||
				!row.HasMember("item_quantity") || !row.HasMember("collision_field") ||
				!row.HasMember("expiry_duration") || !row.HasMember("mod_date_time"))
			{
				return false;
			}
			const auto found = std::ranges::find(current.inventory, row["item_id"].GetUint(),
				&marketplace_store::inventory_record::item_id);
			if (found == current.inventory.end())
			{
				row["item_quantity"].SetUint(0);
				continue;
			}
			if ((found->player_id && found->player_id != user) ||
				(!found->account_type.empty() && found->account_type != "steam"))
			{
				return false;
			}
			row["item_quantity"].SetUint(found->quantity);
			row["collision_field"].SetUint(found->collision_field);
			if (found->expiry_duration)
			{
				row["expiry_duration"].SetUint64(found->expiry_duration);
			}
			else
			{
				row["expiry_duration"].SetNull();
			}
			row["mod_date_time"].SetUint(found->mod_date_time);
		}
		// 0x27C1D0 applies absolute inventory without checking modification time.
		// Replaying the old quantities can resurrect already-pawned duplicates in
		// the native cache. Keep the committed cards/transaction unchanged; only
		// the delivery's inventory snapshot follows the authoritative store.
		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> writer{buffer};
		document.Accept(writer);
		if (buffer.GetSize() > marketplace_store::max_response_json_length)
		{
			return false;
		}
		response.assign(buffer.GetString(), buffer.GetSize());
		return true;
	}
}
