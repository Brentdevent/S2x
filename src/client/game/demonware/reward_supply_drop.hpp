#pragma once

#include "loot_catalog.hpp"
#include "marketplace_store.hpp"
#include "reward_action.hpp"

namespace demonware::reward_supply_drop
{
	inline constexpr std::string_view action = "open_supply_drop";
	enum class open_failure
	{
		none, unsupported_supply_drop, missing_supply_drop, insufficient_quantity,
		missing_eligible_items, invalid_state, catalog_unavailable, expired,
	};
	struct request
	{
		std::string client_tx;
		std::string supply_drop_id;
	};
	bool parse_request(std::string_view json, request& parsed);
	marketplace_store::transaction_result process(const std::string& client_tx,
		const std::string& supply_drop_id, std::uint64_t user_id,
		const std::shared_ptr<const loot_catalog::catalog>& catalog,
		std::uint32_t modification_time, open_failure& failure, float rarity_scale = 1.0f);
	reward::action_result handle(std::string_view json, std::uint64_t user_id,
		const std::shared_ptr<const loot_catalog::catalog>& catalog,
		std::uint32_t modification_time, float rarity_scale = 1.0f);
}
