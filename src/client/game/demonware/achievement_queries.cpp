#include <std_include.hpp>
#include "achievement_queries.hpp"
#include "achievement_orders.hpp"
#include "achievement_response.hpp"
#include "game/types/demonware.hpp"

namespace demonware::achievement_queries
{
	namespace
	{
		std::optional<std::string> user_achievements(const std::string_view json)
		{
			achievement_response::user_achievements_request request{};
			if (!achievement_response::parse_get_user_achievements_request(json, request))
			{
				return std::nullopt;
			}

			return achievement_response::make_get_user_achievements_response(request, achievement_store::get_all());
		}

		std::optional<std::string> scheduled_achievements(const std::string_view json)
		{
			std::string transaction{};
			if (!achievement_response::parse_get_scheduled_user_achievements_request(json, transaction))
			{
				return std::nullopt;
			}

			return achievement_orders::scheduled_response(transaction, static_cast<std::uint64_t>(time(nullptr)));
		}

		std::optional<std::string> respond(const std::string_view action, const std::string_view json)
		{
			if (action == achievement_response::get_user_achievements_action)
			{
				return user_achievements(json);
			}

			if (action == achievement_response::get_scheduled_user_achievements_action)
			{
				return scheduled_achievements(json);
			}

			return std::nullopt;
		}
	}

	reward::action_result handle(const std::string_view action, const std::string_view json)
	{
		// An unavailable store must not become a successful empty cache refresh.
		if (!achievement_store::is_available())
		{
			return {game::demonware::BD_SERVICE_NOT_AVAILABLE};
		}

		auto response = respond(action, json);
		if (!response)
		{
			return {game::demonware::BD_REWARD_EVENTS_DATA_ERROR};
		}

		return {0, std::move(*response)};
	}
}
