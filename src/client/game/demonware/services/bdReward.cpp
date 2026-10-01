#include <std_include.hpp>
#include "../dw_include.hpp"

#include "component/console/console.hpp"
#include "component/hidden_challenge_relay.hpp"
#include "component/hidden_challenges.hpp"

#include "game/game.hpp"
#include "game/demonware/achievement_response.hpp"
#include "game/demonware/loot_service.hpp"
#include "game/demonware/reward_game_event.hpp"

#include "steam/steam.hpp"

namespace demonware
{
	namespace
	{
		bool read_reward_events(byte_buffer* buffer, std::string& context,
			std::vector<std::string>& events)
		{
			// bdReward::reportRewardEvents (0xA47B80): context, uint16 count,
			// then bdJSONData objects (int32 representation 1 + string).
			unsigned short count{};
			if (buffer->size() > 0x1FFFF + 15 || !buffer->read_string(&context, 15) ||
				!buffer->read_uint16(&count) || !count)
			{
				return false;
			}

			for (unsigned int i = 0; i < count; ++i)
			{
				int representation{};
				std::string json{};
				if (!buffer->read_int32(&representation) || representation != 1 ||
					!buffer->read_string(&json, 0x1FFFF))
				{
					return false;
				}
				events.push_back(std::move(json));
			}

			const auto padding = buffer->get_remaining();
			return padding.size() <= 15 && std::ranges::all_of(padding,
				[](const char value) { return value == '\0'; });
		}

		void send_reward_response(service_server* server, const std::uint64_t user_id,
			const std::string& context, const std::string& json,
			const std::uint32_t type = BD_REWARD_EVENT_MESSAGE)
		{
			// The native lobby push handler (0xA35FD0) dispatches this to
			// AE_ProcessResponse. The task acknowledgement alone cannot do that.
			byte_buffer message{};
			message.write_uint32(type);
			message.write_ubyte(1); // Protocol version.
			message.write_uint64(user_id);
			message.write_string("steam");
			message.write_string(context);
			message.write_int32(1); // JSON representation.
			message.write_uint32(1); // One event.
			message.write_int32(1); // bdJSONData representation.
			message.write_string(json);
			server->create_message(BD_LOBBY_SERVICE_PUSH_MESSAGE).send(&message, true);
		}

		void handle_local_game_events(service_server* server,
			const std::vector<reward_game_events::event>& events)
		{
			if (game::environment::is_dedicated())
			{
				return;
			}

			for (const auto& event : events)
			{
				if (event.name != "picked_up_payroll")
				{
					continue;
				}

				if (const auto response = loot_service::collect_payroll())
				{
					send_reward_response(server, steam::SteamUser()->GetSteamID().bits, {}, *response,
						BD_REWARD_ACHIEVEMENT_MESSAGE);
				}
			}
		}

		void submit_hidden_challenge_events(std::vector<reward_game_events::event>& events)
		{
			for (auto& event : events)
			{
				hidden_challenges::submit_reward_game_event(std::move(event));
			}
		}
	}

	bdReward::bdReward() : service(139, "bdReward")
	{
		this->register_task(1, &bdReward::incrementTime);
		this->register_task(2, &bdReward::claimRewardRoll);
		this->register_task(3, &bdReward::claimClientAchievements);
		this->register_task(4, &bdReward::reportRewardEvents);
		this->register_task(5, &bdReward::reportRewardEventsSync);

		this->register_task(11, &bdReward::reportRewardGameEventsForUsers);
		this->register_task(12, &bdReward::reportRewardGameEvents);
	}

	void bdReward::incrementTime(service_server* server, byte_buffer* /*buffer*/) const
	{
		// TODO:
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdReward::claimRewardRoll(service_server* server, byte_buffer* /*buffer*/) const
	{
		// TODO:
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdReward::claimClientAchievements(service_server* server, byte_buffer* /*buffer*/) const
	{
		// TODO:
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdReward::reportRewardEvents(service_server* server, byte_buffer* buffer) const
	{
		std::string context{};
		std::vector<std::string> events{};
		if (!read_reward_events(buffer, context, events))
		{
			server->create_reply(this->task_id(), BD_PARAM_PARSE_ERROR).send();
			return;
		}

		server->create_reply(this->task_id()).send();
		for (const auto& json : events)
		{
			rapidjson::Document request{};
			request.Parse(json.data(), json.size());
			if (request.HasParseError() || !request.IsObject() ||
				!request.HasMember("Action") || !request["Action"].IsString() ||
				!request.HasMember("ClientTx") || !request["ClientTx"].IsString())
			{
				continue;
			}

			const auto& transaction = request["ClientTx"];
			const std::string_view client_tx{transaction.GetString(), transaction.GetStringLength()};
			if (client_tx.empty() || client_tx.size() > 24 || client_tx.find('\0') != std::string_view::npos)
			{
				continue;
			}

			const std::string_view action{request["Action"].GetString(), request["Action"].GetStringLength()};
			std::optional<std::string> response{};
			if (action == "get_user_achievements")
			{
				response = achievement_response::make_get_user_achievements_response(client_tx);
			}
			else if (!game::environment::is_dedicated())
			{
				response = loot_service::handle_action(action, client_tx, request);
			}

			if (!response)
			{
				console::demonware("[DW] bdReward: unhandled action '%.*s'\n",
					static_cast<int>(action.size()), action.data());
				continue;
			}

			send_reward_response(server, steam::SteamUser()->GetSteamID().bits, context, *response);
			console::demonware("[DW] bdReward: answered %.*s (%.*s)\n",
				static_cast<int>(action.size()), action.data(),
				static_cast<int>(client_tx.size()), client_tx.data());
		}
	}

	void bdReward::reportRewardGameEventsForUsers(service_server* server, byte_buffer* buffer) const
	{
		std::vector<reward_game_events::user_event_batch> users{};
		if (reward_game_events::parse_report_for_users_request(buffer, users))
		{
			const auto dedicated = game::environment::is_dedicated();
			const auto local_user_id = dedicated ? 0 : steam::SteamUser()->GetSteamID().bits;
			for (const auto& user : users)
			{
				if (user.account_type != "steam")
				{
					continue;
				}

				if (!dedicated && user.user_id == local_user_id)
				{
					handle_local_game_events(server, user.events);
				}

				for (const auto& event : user.events)
				{
					std::uint32_t group{};
					std::uint32_t challenge{};
					if (!hidden_challenges::get_completion(event, group, challenge))
					{
						continue;
					}

					console::debug(
						"[DW] bdReward: task '11' XUID %llu: zombies [3=%u, 4=%u]\n",
						static_cast<unsigned long long>(user.user_id), group, challenge);
					if (!dedicated && user.user_id == local_user_id)
					{
						hidden_challenges::submit_completion(group, challenge);
					}
					else
					{
						hidden_challenge_relay::submit(user.user_id, group, challenge);
					}
				}
			}
		}
		else
		{
			console::debug("[DW] bdReward: ignored a malformed task '11' request\n");
		}

		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdReward::reportRewardEventsSync(service_server* server, byte_buffer* buffer) const
	{
		// TODO:
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdReward::reportRewardGameEvents(service_server* server, byte_buffer* buffer) const
	{
		std::vector<reward_game_events::event> events{};
		if (reward_game_events::parse_report_request(buffer, events))
		{
			handle_local_game_events(server, events);
			submit_hidden_challenge_events(events);
		}
		else
		{
			console::debug("[DW] bdReward: ignored a malformed task '12' request\n");
		}

		auto reply = server->create_reply(this->task_id());
		reply.send();
	}
}
