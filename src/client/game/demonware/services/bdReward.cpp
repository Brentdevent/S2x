#include <std_include.hpp>
#include "../dw_include.hpp"
#include "game/demonware/achievement_claim.hpp"

#include "component/console/console.hpp"
#include "component/hidden_challenge_relay.hpp"
#include "component/hidden_challenges.hpp"
#include "game/game.hpp"
#include "game/demonware/achievement_response.hpp"
#include "game/demonware/reward_game_event.hpp"
#include "game/demonware/reward_json.hpp"
#include "game/demonware/reward_task4.hpp"
#include "game/demonware/runtime_context.hpp"
#include "game/demonware/hq_rewards.hpp"

namespace demonware
{
	namespace
	{
		bool settle_local_events(service_server* server, const std::vector<reward_game_events::event>& events,
			const std::uint64_t user)
		{
			if (!user)
			{
				return true;
			}
			const auto catalog = loot_catalog::get_snapshot();
			for (const auto& event : events)
			{
				std::string push;
				if (!hq_rewards::process(event, user, static_cast<std::uint32_t>(time(nullptr)), catalog.get(), push))
				{
					return false;
				}
				if (push.empty())
				{
					continue;
				}
				bdRewardEvent notification;
				notification.push_type = 0x43;
				notification.r2 = 1;
				notification.user_id = user;
				notification.platform1 = "steam";
				notification.platform2 = "s2_steam";
				notification.rewardEventType = 1;
				notification.r7 = notification.r8 = 1;
				notification.json_buffer = std::move(push);
				byte_buffer data;
				notification.serialize(&data);
				server->create_message(BD_LOBBY_SERVICE_PUSH_MESSAGE).send(&data, true);
			}
			return true;
		}

		void acknowledge_game_events(service_server* server, const std::uint8_t task)
		{
			class empty_result final : public bdTaskResult
			{
			public:
				void serialize(byte_buffer* data) override
				{
					char empty{};
					data->write_struct(&empty, 0);
				}
			};
			// Tasks 11/12 use bdStructBufferTask. Count-framed replies fail SDK
			// deserialization and requeue already delivered events (error 4).
			auto reply = server->create_reply(task);
			auto result = std::make_unique<empty_result>();
			reply.add(result);
			reply.send_struct();
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
		if (achievement_claim::try_handle(server, buffer))
		{
			return;
		}
		reward_task4::execution_context context;
		context.dedicated = game::environment::is_dedicated(); // Immutable launch configuration.
		const auto identity = runtime_context::get_snapshot();
		context.user_id = identity ? identity->user_id : 0;
		context.loot_rarity_scale = identity ? identity->loot_rarity_scale : 1.0f;
		context.catalog = loot_catalog::get_snapshot();
		context.modification_time = reward_json::modification_time();
		(void)reward_task4::handle(server, buffer, context);
	}

	void bdReward::reportRewardGameEventsForUsers(service_server* server, byte_buffer* buffer) const
	{
		reward_game_events::report_for_users_request request{};
		if (reward_game_events::parse_report_for_users_request(buffer, request))
		{
			const auto dedicated = game::environment::is_dedicated();
			const auto identity = runtime_context::get_snapshot();
			const auto local_user_id = dedicated || !identity ? 0 : identity->user_id;
			for (const auto& user : request.users)
			{
				if (user.account_type != "steam")
				{
					continue;
				}

				if (user.user_id == local_user_id && !settle_local_events(server, user.events, local_user_id))
				{
					server->create_reply(this->task_id(), BD_REWARD_EVENTS_TRANSACTION_ERROR).send_struct();
					return;
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
			console::debug("[DW] bdReward: rejected a malformed task '11' request\n");
			server->create_reply(this->task_id(), BD_PARAM_PARSE_ERROR).send_struct();
			return;
		}

		acknowledge_game_events(server, this->task_id());
	}

	void bdReward::reportRewardEventsSync(service_server* server, byte_buffer* buffer) const
	{
		std::string context;
		std::string json;
		std::uint16_t count{};
		std::int32_t type{};
		if (!buffer || !buffer->read_string(&context, 16) || context != "s2_steam" ||
			!buffer->read_uint16(&count) || count != 1 || !buffer->read_int32(&type) || type != 1 ||
			!buffer->read_string(&json, achievement_response::maximum_request_length) ||
			!buffer->has_only_zero_padding(16))
		{
			server->create_reply(this->task_id(), BD_PARAM_PARSE_ERROR).send();
			return;
		}
		achievement_response::user_achievements_request request;
		std::uint64_t user_id{};
		if (!achievement_response::parse_get_user_achievements_for_users_request(json, request, user_id))
		{
			server->create_reply(this->task_id(), BD_REWARD_EVENTS_DATA_ERROR).send();
			return;
		}
		const auto identity = runtime_context::get_snapshot();
		// This offline store belongs to the local player. A server must never
		// project that player's achievements onto another connected account.
		if (game::environment::is_dedicated() || !identity || identity->user_id != user_id)
		{
			server->create_reply(this->task_id(), BD_SERVICE_NOT_AVAILABLE).send();
			return;
		}
		const auto response = achievement_response::make_get_user_achievements_for_users_response(
			request, user_id, achievement_store::get_all());
		if (!response)
		{
			server->create_reply(this->task_id(), BD_REWARD_EVENTS_DATA_ERROR).send();
			return;
		}
		class sync_result final : public bdTaskResult
		{
		public:
			std::string json;
			void serialize(byte_buffer* data) override
			{
				// 0xA3B7C0 reads an inner reward event: type 1 followed by JSON.
				data->write_int32(1);
				data->write_string(json);
			}
		};
		auto result = std::make_unique<sync_result>();
		result->json = *response;
		auto reply = server->create_reply(this->task_id());
		reply.add(result);
		reply.send();
	}

	void bdReward::reportRewardGameEvents(service_server* server, byte_buffer* buffer) const
	{
		reward_game_events::report_request request{};
		if (reward_game_events::parse_report_request(buffer, request))
		{
			const auto identity = runtime_context::get_snapshot();
			const auto user = game::environment::is_dedicated() || !identity ? 0 : identity->user_id;
			if (!settle_local_events(server, request.events, user))
			{
				server->create_reply(this->task_id(), BD_REWARD_EVENTS_TRANSACTION_ERROR).send_struct();
				return;
			}
			submit_hidden_challenge_events(request.events);
		}
		else
		{
			console::debug("[DW] bdReward: rejected a malformed task '12' request\n");
			server->create_reply(this->task_id(), BD_PARAM_PARSE_ERROR).send_struct();
			return;
		}

		acknowledge_game_events(server, this->task_id());
	}
}
