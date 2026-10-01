#include <std_include.hpp>
#include "economy_tools.hpp"
#include "loot_catalog.hpp"

#include <charconv>

namespace demonware::economy_tools
{
	bool parse_number(std::string_view text, std::uint32_t& value)
	{
		auto base = 10;
		if (text.starts_with("0x") || text.starts_with("0X"))
		{
			base = 16;
			text.remove_prefix(2);
		}
		if (text.empty())
		{
			return false;
		}
		const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
		return result.ec == std::errc{} && result.ptr == text.data() + text.size();
	}

	const char* currency_name(const std::uint32_t id)
	{
		switch (id)
		{
		case 2: return "COD Points";
		case 6: return "Armory Credits";
		case 7: return "Social Score";
		default: return nullptr;
		}
	}

	bool succeeded(const marketplace_store::transaction_status status)
	{
		return status == marketplace_store::transaction_status::committed ||
			status == marketplace_store::transaction_status::replayed;
	}

	marketplace_store::transaction_result give_item(const std::uint32_t id, const std::uint32_t amount,
		const std::uint64_t user, const std::string& transaction)
	{
		if (!id || !amount || amount > INT32_MAX || !user)
		{
			return {};
		}
		const auto catalog = loot_catalog::get_snapshot();
		if (!catalog || (!std::ranges::any_of(catalog->items, [id](const auto& row)
			{ return row.item.item_id == id && row.reference_valid; }) &&
			!std::ranges::any_of(catalog->supply_drops, [id](const auto& row) { return row.item_id == id; })))
		{
			return {};
		}
		return marketplace_store::transact(transaction,
			"giveItem:" + std::to_string(user) + ":" + std::to_string(id) + ":" + std::to_string(amount),
			[&](marketplace_store::transaction& state, std::string& receipt)
			{
				auto row = state.get_inventory(id).value_or(marketplace_store::inventory_record{});
				// Preserve current metadata. Do not convert foreign/colliding/rental
				// rows into local permanent ownership or overflow native signed counts.
				if ((row.player_id && row.player_id != user) ||
					(!row.account_type.empty() && row.account_type != "steam") || row.collision_field ||
					!((!row.expire_date_time && !row.expiry_duration) ||
						(row.expire_date_time == UINT32_MAX && row.expiry_duration == INT64_MAX)) ||
					row.quantity > INT32_MAX - amount)
				{
					return false;
				}
				row.item_id = id;
				row.player_id = user;
				row.account_type = "steam";
				row.quantity += amount;
				row.mod_date_time = static_cast<std::uint32_t>(std::time(nullptr));
				if (state.set_inventory(row) != marketplace_store::edit_result::updated)
				{
					return false;
				}
				receipt = "{}";
				return true;
			});
	}

	marketplace_store::transaction_result give_currency(const std::uint32_t id, const std::uint32_t amount,
		const std::uint64_t user, const std::string& transaction)
	{
		if (!currency_name(id) || !amount || amount > INT32_MAX || !user)
		{
			return {};
		}
		return marketplace_store::transact(transaction,
			"giveCurrency:" + std::to_string(user) + ":" + std::to_string(id) + ":" + std::to_string(amount),
			[&](marketplace_store::transaction& state, std::string& receipt)
			{
				const auto currency = static_cast<std::uint8_t>(id);
				if (state.get_currency(currency) > INT32_MAX - amount ||
					state.add_currency(currency, amount) != marketplace_store::edit_result::updated)
				{
					return false;
				}
				receipt = "{}";
				return true;
			});
	}
}
