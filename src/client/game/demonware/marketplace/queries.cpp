#include <std_include.hpp>
#include "queries.hpp"
#include "pagination.hpp"
#include "store.hpp"
#include "inventory_expiry.hpp"
#include "promotional_vouchers.hpp"
#include "game/types/demonware.hpp"

namespace demonware::marketplace_queries
{
	result balance(byte_buffer* buffer)
	{
		using namespace game::demonware;
		std::string context;
		std::uint32_t maximum_results{};
		if (!buffer || !buffer->read_string(&context, 16) ||
			!buffer->read_uint32(&maximum_results) || maximum_results > 13 ||
			!buffer->has_only_zero_padding() || context != "s2_steam")
		{
			return {BD_PARAM_PARSE_ERROR};
		}

		const auto state = marketplace_store::get_snapshot();
		if (state.status != marketplace_store::store_status::ready)
		{
			return {BD_MARKETPLACE_STORAGE_ERROR};
		}

		result response;
		const auto count = std::min<std::size_t>(state.currencies.size(), maximum_results);
		for (std::size_t index = 0; index < count; ++index)
		{
			auto record = std::make_unique<bdMarketplaceCurrency>();
			record->m_currencyId = state.currencies[index].currency_id;
			record->m_value = state.currencies[index].value;
			response.records.emplace_back(std::move(record));
		}

		return response;
	}

	result inventory(byte_buffer* buffer, const std::uint64_t fallback_user_id,
		const bool dedicated)
	{
		using namespace game::demonware;
		std::string context;
		std::uint32_t first_wire_field{}, second_wire_field{};
		if (!buffer || !buffer->read_string(&context, 16) ||
			!buffer->read_uint32(&first_wire_field) || !buffer->read_uint32(&second_wire_field) ||
			!buffer->has_only_zero_padding() || context != "s2_steam")
		{
			return {BD_PARAM_PARSE_ERROR};
		}

		const auto pagination = marketplace_pagination::parse_wire_fields(
			first_wire_field, second_wire_field);
		if (!pagination)
		{
			return {BD_PARAM_PARSE_ERROR};
		}
		if (!dedicated && !fallback_user_id)
		{
			return {BD_SERVICE_NOT_AVAILABLE};
		}

		// Seed the stock voucher inbox before its initial Task 165 snapshot. The
		// delivery receipt makes later pages/reconnects read-only; failure remains
		// in the existing native fetch/retry path, with no partially delivered mail.
		if (!dedicated && !promotional_vouchers::deliver(fallback_user_id))
		{
			return {BD_MARKETPLACE_STORAGE_ERROR};
		}

		const auto state = marketplace_store::get_snapshot();
		if (state.status != marketplace_store::store_status::ready)
		{
			return {BD_MARKETPLACE_STORAGE_ERROR};
		}

		const auto page = marketplace_pagination::get_page_range(state.inventory.size(),
			pagination->items_per_page, pagination->page_number);
		if (!page)
		{
			return {BD_PARAM_PARSE_ERROR};
		}

		result response;
		for (auto index = page->first; index < page->last; ++index)
		{
			const auto& entry = state.inventory[index];
			// The selected local profile owns these rows. Present them under the
			// current runtime ID without rewriting historical owners on disk.
			if (!dedicated && !marketplace_store::owns_inventory(entry, fallback_user_id))
			{
				return {BD_MARKETPLACE_STORAGE_ERROR};
			}

			auto record = std::make_unique<bdMarketplaceInventory>();
			record->m_playerId = dedicated && entry.player_id ? entry.player_id : fallback_user_id;
			record->unk = entry.account_type.empty() ? "steam" : entry.account_type;
			record->m_itemId = entry.item_id;
			record->m_itemQuantity = entry.quantity;
			record->m_itemXp = entry.item_xp;
			record->m_itemData = entry.item_data;
			const auto expiry = inventory_expiry::to_wire(entry.expire_date_time, entry.expiry_duration);
			record->m_expireDateTime = expiry.date;
			record->m_expiryDuration = expiry.duration;
			record->m_collisionField = entry.collision_field;
			record->m_modDateTime = entry.mod_date_time;
			response.records.emplace_back(std::move(record));
		}

		return response;
	}
}
