#include <std_include.hpp>
#include "dw_include.hpp"
#include "achievement_claim.hpp"
#include "achievement_response.hpp"
#include "reward_task4.hpp"
#include <set>
#include "component/achievement_sync.hpp"
#include "game/game.hpp"

namespace demonware::achievement_claim
{
	bool try_handle(service_server* server, byte_buffer* buffer)
	{
		if (!buffer)
		{
			return false;
		}

		auto input = *buffer;

		std::string context, json;
		std::uint16_t count{};
		std::int32_t type{};

		if (!input.read_string(&context, 16) || !input.read_uint16(&count) || count != 1 ||
			!input.read_int32(&type) || type != 1 || !input.read_string(&json, 6143))
		{
			return false;
		}
		
		rapidjson::Document request;
		request.Parse(json.data(), json.size());
		if (request.HasParseError() || !request.IsObject() || !request.HasMember("Action") ||
			!request["Action"].IsString())
		{
			return false;
		}

		const auto fetching = request["Action"] == achievement_response::get_user_achievements_action.data();
		if (!fetching && request["Action"] != action)
		{
			return false;
		}

		if (context != "s2_steam" || !input.has_only_zero_padding(16))
		{
			if (fetching)
			{
				return false;
			}
			server->create_reply(4, BD_PARAM_PARSE_ERROR).send();
			return true;
		}

		const auto user = achievement_sync::local_user_id();
		if (!user || game::environment::is_dedicated())
		{
			if (fetching)
			{
				return false;
			}
			server->create_reply(4, BD_SERVICE_NOT_AVAILABLE).send();
			return true;
		}

		const auto send = [&](const std::uint32_t push_type, const std::string& payload)
		{
			bdRewardEvent event;
			event.push_type = push_type;
			event.r2 = 1;
			event.user_id = user;
			event.platform1 = "steam";
			event.platform2 = context;
			event.rewardEventType = 1;
			event.r7 = 1;
			event.r8 = 1;
			event.json_buffer = payload;
			byte_buffer data;
			event.serialize(&data);
			server->create_message(BD_LOBBY_SERVICE_PUSH_MESSAGE).send(&data, true);
		};

		const auto send_state = [&](const achievement_record& record, const char* reason)
		{
			rapidjson::Document push;
			auto& alloc = push.GetAllocator();
			push.CopyFrom(serialize_achievement(record, alloc), alloc);
			if (!push.IsObject())
			{
				return false;
			}
			push.AddMember("reason", rapidjson::Value{reason, alloc}, alloc);
			push.AddMember("type", "CHALLENGE", alloc);
			push.AddMember("triggers", rapidjson::Value{rapidjson::kArrayType}, alloc);
			rapidjson::StringBuffer encoded;
			rapidjson::Writer<rapidjson::StringBuffer> writer{encoded};
			push.Accept(writer);
			send(0x43, {encoded.GetString(), encoded.GetSize()});
			return true;
		};

		if (fetching)
		{
			achievement_response::user_achievements_request query;
			if (!achievement_response::parse_get_user_achievements_request(json, query))
			{
				return false;
			}

			reward_task4::execution_context execution;
			execution.user_id = user;
			if (reward_task4::handle(server, buffer, execution).action.error)
			{
				return true;
			}

			// 0x13E960 caches finished periodic records. apply terminal states afterwards through AE_HandleAchievementPush (0x13C480),
			// just as a successful claim does, without replaying inventory triggers.
			// The expired list is process-local; rebuild it after restart, but do not
			// re-add cards dismissed by the player on every menu fetch.
			static std::set<std::pair<std::string, std::uint64_t>> notified;
			for (const auto& record : achievement_store::get_all())
			{
				if (!achievement_kind::periodic(record.kind) || !record.activation_timestamp)
				{
					continue;
				}
				const auto expired = record.status == achievement_status::inactive &&
					record.usage_time_remaining == 0 && record.expiration_timestamp;
				const auto completed = record.status == achievement_status::finished && record.completion_timestamp;
				if (!expired && !completed)
				{
					continue;
				}
				const auto key = std::make_pair(record.name, *record.activation_timestamp);
				if (expired && notified.contains(key))
				{
					continue;
				}
				if (send_state(record, expired ? "expired" : "completed") && expired)
				{
					notified.insert(key);
				}
			}

			return true;
		}

		const auto response = process(json, user, static_cast<std::uint32_t>(time(nullptr)));
		if (response.acknowledgement.empty())
		{
			server->create_reply(4, response.error).send();
			return true;
		}

		server->create_reply(4).send();

		if (!response.error)
		{
			if (!response.achievement_push.empty())
			{
				send(0x43, response.achievement_push);
			}

			const auto records = achievement_store::get_all();

			const auto claimed = std::ranges::find(records, request["AchievementName"].GetString(), &achievement_record::name);
			if (claimed != records.end() && (claimed->kind == 1 || claimed->kind == 2))
			{
				const auto name = claimed->kind == 1 ? "above_beyond_daily" : "above_beyond_weekly";
				const auto meta = std::ranges::find(records, name, &achievement_record::name);
				if (meta != records.end())
				{
					send_state(*meta, meta->progress ? "inProgress" : "completed");
				}
			}
		}

		send(BD_REWARD_EVENT_MESSAGE, response.acknowledgement);

		achievement_sync::request_refresh();
		return true;
	}
}
