#include <std_include.hpp>
#include "achievement_queries.hpp"
#include "achievement_response.hpp"
#include "achievement_orders.hpp"
#include "game/types/demonware.hpp"

namespace demonware::achievement_queries
{
	reward::action_result handle(const std::string_view action, const std::string_view json)
	{
		std::optional<std::string> response;
		if (action == achievement_response::get_user_achievements_action)
		{
			achievement_response::user_achievements_request request;
			if (achievement_response::parse_get_user_achievements_request(json, request))
			{
				response = achievement_response::make_get_user_achievements_response(
					request, achievement_store::get_all());
			}
		}
		else if (action == achievement_response::get_scheduled_user_achievements_action)
		{
			std::string transaction;
			if (achievement_response::parse_get_scheduled_user_achievements_request(json, transaction))
			{
				response = achievement_orders::scheduled_response(transaction, static_cast<std::uint64_t>(time(nullptr)));
			}
		}
		if (!response)
		{
			return {game::demonware::BD_REWARD_EVENTS_DATA_ERROR};
		}
		return {0, std::move(*response)};
	}
}
