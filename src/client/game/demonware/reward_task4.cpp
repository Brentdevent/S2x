#include <std_include.hpp>
#include "dw_include.hpp"
#include "reward_task4.hpp"
#include "achievement_orders.hpp"
#include "achievement_queries.hpp"
#include "achievement_response.hpp"
#include "reward_end_mission.hpp"
#include "reward_json.hpp"
#include "reward_push.hpp"
#include "reward_supply_drop.hpp"

namespace demonware::reward_task4
{
	namespace
	{
		struct request
		{
			std::string context{};
			std::string json{};
			std::string action{};
		};

		std::optional<request> parse_request(byte_buffer* buffer, std::uint32_t& error)
		{
			request result{};
			std::uint16_t count{};
			std::int32_t type{};
			if (!buffer || !buffer->read_string(&result.context, 16) || result.context != "s2_steam" ||
				!buffer->read_uint16(&count) || count != 1 ||
				!buffer->read_int32(&type) || type != 1 ||
				!buffer->read_string(&result.json, 6143) || !buffer->has_only_zero_padding(16))
			{
				error = BD_PARAM_PARSE_ERROR;
				return {};
			}

			rapidjson::Document document{};
			document.Parse(result.json.data(), result.json.size());

			error = BD_REWARD_EVENTS_DATA_ERROR;
			if (document.HasParseError() || !reward_json::unique_members(document))
			{
				return {};
			}

			const auto action = document.FindMember("Action");
			if (action == document.MemberEnd() || !action->value.IsString())
			{
				return {};
			}

			result.action.assign(action->value.GetString(), action->value.GetStringLength());
			if (!reward_json::bounded_ascii(result.action, 128, true))
			{
				return {};
			}

			return result;
		}

		reward::action_result dispatch(const request& request, const execution_context& context)
		{
			const std::string_view action = request.action;

			if (action == reward_supply_drop::action)
			{
				return reward_supply_drop::handle(request.json, context.user_id, context.catalog,
					context.modification_time, context.loot_rarity_scale);
			}

			if (action == reward_end_mission::action)
			{
				return reward_end_mission::handle(request.json);
			}

			if (action == achievement_orders::activation_action || action == achievement_orders::contract_action)
			{
				auto activated = achievement_orders::activate(request.json, context.user_id,
					static_cast<std::uint64_t>(time(nullptr)));
				return {activated.error, std::move(activated.json)};
			}

			if (action == achievement_response::get_user_achievements_action ||
				action == achievement_response::get_scheduled_user_achievements_action)
			{
				return achievement_queries::handle(action, request.json);
			}

			return {BD_REWARD_EVENTS_NOT_ENABLED};
		}
	}

	std::uint32_t handle(service_server* server, byte_buffer* buffer, const execution_context& context)
	{
		const auto reply_error = [&](const std::uint32_t error)
		{
			server->create_reply(4, error).send();
			return error;
		};

		if (context.dedicated)
		{
			return reply_error(BD_SERVICE_NOT_AVAILABLE);
		}

		std::uint32_t error{};
		const auto request = parse_request(buffer, error);
		if (!request)
		{
			return reply_error(error);
		}

		if (!context.user_id)
		{
			return reply_error(BD_SERVICE_NOT_AVAILABLE);
		}

		const auto result = dispatch(*request, context);
		if (result.error)
		{
			return reply_error(result.error);
		}

		server->create_reply(4).send();
		send_reward_push(server, BD_REWARD_EVENT_MESSAGE, context.user_id, result.response_json, request->context);
		return 0;
	}
}
