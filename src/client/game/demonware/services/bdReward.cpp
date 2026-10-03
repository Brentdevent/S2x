#include <std_include.hpp>
#include "../dw_include.hpp"

#include "component/console/console.hpp"
#include "component/demonware/zombies_progression.hpp"
#include "component/hidden_challenges.hpp"

#include "game/game.hpp"
#include "game/demonware/achievement_claim.hpp"
#include "game/demonware/achievement_response.hpp"
#include "game/demonware/hq_rewards.hpp"
#include "game/demonware/reward_game_event.hpp"
#include "game/demonware/reward_json.hpp"
#include "game/demonware/reward_push.hpp"
#include "game/demonware/reward_task4.hpp"
#include "game/demonware/runtime_context.hpp"

namespace demonware
{
	namespace
	{
		class empty_struct_result final : public bdTaskResult
		{
		public:
			void serialize(byte_buffer* data) override
			{
				char empty{};
				data->write_struct(&empty, 0);
			}
		};

		class reward_event_result final : public bdTaskResult
		{
		public:
			std::string json{};

			void serialize(byte_buffer* data) override
			{
				data->write_int32(1);
				data->write_string(this->json);
			}
		};

		bool settle_local_events(service_server* server, const std::vector<reward_game_events::event>& events,
			const std::uint64_t user)
		{
			if (!user)
			{
				return true;
			}

			const auto catalog = loot_catalog::get_snapshot();
			const auto now = static_cast<std::uint32_t>(time(nullptr));

			for (const auto& event : events)
			{
				std::string push{};
				if (!zombies_progression::process(event, user) ||
					!hq_rewards::process(event, user, now, catalog.get(), push))
				{
					return false;
				}

				if (!push.empty())
				{
					send_reward_push(server, BD_REWARD_ACHIEVEMENT_MESSAGE, user, push);
				}
			}

			return true;
		}

		void submit_hidden_challenge_completions(const reward_game_events::user_event_batch& user,
			const std::uint64_t local_user_id)
		{
			for (const auto& event : user.events)
			{
				std::uint32_t group{};
				std::uint32_t challenge{};
				if (!hidden_challenges::get_completion(event, group, challenge))
				{
					continue;
				}

				console::debug("[DW] bdReward: zombies challenge %u/%u for %llu\n", group, challenge, user.user_id);

				if (local_user_id && user.user_id == local_user_id)
				{
					hidden_challenges::submit_completion(group, challenge);
				}
			}
		}

		void acknowledge_game_events(service_server* server, const std::uint8_t task)
		{
			// Struct-framed tasks; a count-framed reply makes the SDK requeue delivered events
			auto reply = server->create_reply(task);
			auto result = std::make_unique<empty_struct_result>();
			reply.add(result);
			reply.send_struct();
		}

		bool read_sync_request(byte_buffer* buffer, std::string& json)
		{
			std::string context{};
			std::uint16_t count{};
			std::int32_t type{};

			return buffer && buffer->read_string(&context, 16) && context == "s2_steam" &&
				buffer->read_uint16(&count) && count == 1 &&
				buffer->read_int32(&type) && type == 1 &&
				buffer->read_string(&json, achievement_response::maximum_request_length) &&
				buffer->has_only_zero_padding(16);
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
		server->create_reply(this->task_id()).send();
	}

	void bdReward::claimRewardRoll(service_server* server, byte_buffer* /*buffer*/) const
	{
		server->create_reply(this->task_id()).send();
	}

	void bdReward::claimClientAchievements(service_server* server, byte_buffer* /*buffer*/) const
	{
		server->create_reply(this->task_id()).send();
	}

	void bdReward::reportRewardEvents(service_server* server, byte_buffer* buffer) const
	{
		if (achievement_claim::try_handle(server, buffer))
		{
			return;
		}

		const auto identity = runtime_context::get_snapshot();

		reward_task4::execution_context context{};
		context.dedicated = game::environment::is_dedicated();
		context.user_id = identity ? identity->user_id : 0;
		context.loot_rarity_scale = identity ? identity->loot_rarity_scale : 1.0f;
		context.catalog = loot_catalog::get_snapshot();
		context.modification_time = reward_json::modification_time();

		reward_task4::handle(server, buffer, context);
	}

	void bdReward::reportRewardGameEventsForUsers(service_server* server, byte_buffer* buffer) const
	{
		reward_game_events::report_for_users_request request{};
		if (!reward_game_events::parse_report_for_users_request(buffer, request))
		{
			console::debug("[DW] bdReward: malformed game events for users\n");
			server->create_reply(this->task_id(), BD_PARAM_PARSE_ERROR).send_struct();
			return;
		}

		const auto local_user_id = runtime_context::get_local_user_id();

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

			submit_hidden_challenge_completions(user, local_user_id);
		}

		acknowledge_game_events(server, this->task_id());
	}

	void bdReward::reportRewardEventsSync(service_server* server, byte_buffer* buffer) const
	{
		std::string json{};
		if (!read_sync_request(buffer, json))
		{
			server->create_reply(this->task_id(), BD_PARAM_PARSE_ERROR).send();
			return;
		}

		achievement_response::user_achievements_request request{};
		std::uint64_t user_id{};
		if (!achievement_response::parse_get_user_achievements_for_users_request(json, request, user_id))
		{
			server->create_reply(this->task_id(), BD_REWARD_EVENTS_DATA_ERROR).send();
			return;
		}

		const auto local_user_id = runtime_context::get_local_user_id();
		if (!local_user_id || local_user_id != user_id)
		{
			server->create_reply(this->task_id(), BD_SERVICE_NOT_AVAILABLE).send();
			return;
		}

		const auto response = achievement_response::make_get_user_achievements_for_users_response(request, user_id,
			achievement_store::get_all());
		if (!response)
		{
			server->create_reply(this->task_id(), BD_REWARD_EVENTS_DATA_ERROR).send();
			return;
		}

		auto result = std::make_unique<reward_event_result>();
		result->json = *response;

		auto reply = server->create_reply(this->task_id());
		reply.add(result);
		reply.send();
	}

	void bdReward::reportRewardGameEvents(service_server* server, byte_buffer* buffer) const
	{
		reward_game_events::report_request request{};
		if (!reward_game_events::parse_report_request(buffer, request))
		{
			console::debug("[DW] bdReward: malformed game events\n");
			server->create_reply(this->task_id(), BD_PARAM_PARSE_ERROR).send_struct();
			return;
		}

		if (!settle_local_events(server, request.events, runtime_context::get_local_user_id()))
		{
			server->create_reply(this->task_id(), BD_REWARD_EVENTS_TRANSACTION_ERROR).send_struct();
			return;
		}

		for (auto& event : request.events)
		{
			hidden_challenges::submit_reward_game_event(std::move(event));
		}

		acknowledge_game_events(server, this->task_id());
	}
}
