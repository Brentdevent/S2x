#include <std_include.hpp>
#include "economy_tools.hpp"
#include "loot_catalog.hpp"

#include <utils/string.hpp>

#include <charconv>

namespace demonware::economy_tools
{
	namespace
	{
		bool is_known_item(const loot_catalog::catalog& catalog, const std::uint32_t id)
		{
			const auto item = std::ranges::any_of(catalog.items, [id](const auto& row)
			{
				return row.item.item_id == id && row.reference_valid;
			});

			const auto supply_drop = std::ranges::any_of(catalog.supply_drops, [id](const auto& row)
			{
				return row.item_id == id;
			});

			return item || supply_drop;
		}

		bool can_stack(const marketplace_store::inventory_record& row, const std::uint64_t user, const std::uint32_t amount)
		{
			const auto owned_by_user = !row.player_id || row.player_id == user;
			const auto steam_account = row.account_type.empty() || row.account_type == "steam";

			return owned_by_user && steam_account && !row.collision_field && marketplace_store::is_permanent(row) &&
				row.quantity <= INT32_MAX - amount;
		}

		std::string fingerprint(const char* command, const std::uint64_t user, const std::uint32_t id, const std::uint32_t amount)
		{
			return utils::string::va("%s:%llu:%u:%u", command, user, id, amount);
		}
	}

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

		const auto end = text.data() + text.size();
		const auto result = std::from_chars(text.data(), end, value, base);

		return result.ec == std::errc{} && result.ptr == end;
	}

	const char* currency_name(const std::uint32_t id)
	{
		switch (id)
		{
		case 2:
			return "COD Points";
		case 6:
			return "Armory Credits";
		case 7:
			return "Social Score";
		default:
			return nullptr;
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
		if (!catalog || !is_known_item(*catalog, id))
		{
			return {};
		}

		return marketplace_store::transact(transaction, fingerprint("giveItem", user, id, amount),
			[&](marketplace_store::transaction& state, std::string& receipt)
		{
			auto row = state.get_inventory(id).value_or(marketplace_store::inventory_record{});
			if (!can_stack(row, user, amount))
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

		return marketplace_store::transact(transaction, fingerprint("giveCurrency", user, id, amount),
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
