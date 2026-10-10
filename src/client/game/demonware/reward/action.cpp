#include <std_include.hpp>
#include "action.hpp"

#include "game/types/demonware.hpp"

namespace demonware::reward
{
	std::uint32_t transaction_error(const marketplace_store::transaction_status status)
	{
		using enum marketplace_store::transaction_status;
		using namespace game::demonware;

		switch (status)
		{
		case committed:
		case replayed:
			return 0;
		case client_tx_conflict:
			return BD_MARKETPLACE_IDEMPOTENT_REQUEST_COLLISION;
		case invalid_argument:
			return BD_REWARD_EVENTS_DATA_ERROR;
		default:
			return BD_REWARD_EVENTS_TRANSACTION_ERROR;
		}
	}

	action_result transaction_response(marketplace_store::transaction_result transaction)
	{
		const auto error = transaction_error(transaction.status);
		return {error, error ? std::string{} : std::move(transaction.response_json), transaction.status};
	}
}
