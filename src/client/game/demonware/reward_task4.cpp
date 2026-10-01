#include <std_include.hpp>
#include "reward_task4.hpp"
#include "achievement_queries.hpp"
#include "achievement_orders.hpp"
#include "achievement_response.hpp"
#include "reward_json.hpp"
#include "reward_end_mission.hpp"
#include "reward_supply_drop.hpp"
#include "game/types/demonware.hpp"

namespace demonware::reward_task4
{
	result process(byte_buffer* buffer, const execution_context& context)
	{
		using namespace game::demonware;
		result response;
		response.user_id = context.user_id;
		if (context.dedicated)
		{
			response.action.error = BD_SERVICE_NOT_AVAILABLE;
			return response;
		}
		// Stock aligned contract activations can carry a full block of zero padding.
		std::uint16_t count{};
		std::int32_t type{};
		std::string json;
		if (!buffer || !buffer->read_string(&response.context, 16) ||
			!buffer->read_uint16(&count) || count != 1 ||
			!buffer->read_int32(&type) || type != 1 ||
			!buffer->read_string(&json, 6143) || !buffer->has_only_zero_padding(16) ||
			response.context != "s2_steam")
		{
			response.action.error = BD_PARAM_PARSE_ERROR;
			return response;
		}
		rapidjson::Document document;
		document.Parse(json.data(), json.size());
		if (document.HasParseError() || !reward_json::unique_members(document) ||
			!document.HasMember("Action") || !document["Action"].IsString())
		{
			response.action.error = BD_REWARD_EVENTS_DATA_ERROR;
			return response;
		}
		const std::string_view action{document["Action"].GetString(),
			document["Action"].GetStringLength()};
		if (!reward_json::bounded_ascii(action, 128, true))
		{
			response.action.error = BD_REWARD_EVENTS_DATA_ERROR;
			return response;
		}

		// Identity is an owned main-thread publication, never a Steam call here.
		if (!context.user_id)
		{
			response.action.error = BD_SERVICE_NOT_AVAILABLE;
			return response;
		}
		if (action == reward_supply_drop::action)
			response.action = reward_supply_drop::handle(json, context.user_id,
				context.catalog, context.modification_time, context.loot_rarity_scale);
		else if (action == reward_end_mission::action)
			response.action = reward_end_mission::handle(json);
		else if (action == achievement_orders::activation_action || action == achievement_orders::contract_action)
		{
			auto activated = achievement_orders::activate(json, context.user_id,
				static_cast<std::uint64_t>(time(nullptr)));
			response.action = {activated.error, std::move(activated.json)};
		}
		else if (action == achievement_response::get_user_achievements_action ||
			action == achievement_response::get_scheduled_user_achievements_action)
			response.action = achievement_queries::handle(action, json);
		else
			response.action.error = BD_REWARD_EVENTS_NOT_ENABLED;
		return response;
	}

	void deliver(service_server* server, const result& response)
	{
		using namespace game::demonware;
		if (response.action.error)
		{
			server->create_reply(4, response.action.error).send();
			return;
		}
		// Build the exact stock envelope from the committed/cached response. No
		// action, mutation or JSON regeneration occurs during delivery/replay.
		bdRewardEvent event{};
		event.push_type = BD_REWARD_EVENT_MESSAGE;
		event.r2 = 1;
		event.user_id = response.user_id;
		event.platform1 = "steam";
		event.platform2 = response.context;
		event.rewardEventType = 1;
		event.r7 = 1;
		event.r8 = 1;
		event.json_buffer = response.action.response_json;
		byte_buffer push_buffer;
		event.serialize(&push_buffer);
		// The stock task acknowledgement must precede its reward notification.
		server->create_reply(4).send();
		auto push = server->create_message(BD_LOBBY_SERVICE_PUSH_MESSAGE);
		push.send(&push_buffer, true);

	}

	result handle(service_server* server, byte_buffer* buffer, const execution_context& context)
	{
		auto response = process(buffer, context);
		deliver(server, response);
		return response;
	}
}
