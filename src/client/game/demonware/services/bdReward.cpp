#include <std_include.hpp>
#include "../dw_include.hpp"

#include "component/console/console.hpp"
#include "component/hidden_challenges.hpp"

#include "game/demonware/reward_game_event.hpp"

#include "steam/steam.hpp"

namespace demonware
{
	namespace
	{
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

	void bdReward::reportRewardEvents(service_server* server, byte_buffer* /*buffer*/) const
	{
		// TODO:
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdReward::reportRewardGameEventsForUsers(service_server* server, byte_buffer* buffer) const
	{
		std::vector<reward_game_events::user_event_batch> users{};
		if (reward_game_events::parse_report_for_users_request(buffer, users))
		{
			const auto local_user_id = steam::SteamUser()->GetSteamID().bits;
			for (auto& user : users)
			{
				if (user.user_id == local_user_id && user.account_type == "steam")
				{
					submit_hidden_challenge_events(user.events);
				}
			}
		}
		else
		{
			console::debug("[hidden_challenges] ignored a malformed bdReward task 11 request\n");
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
			submit_hidden_challenge_events(events);
		}
		else
		{
			console::debug("[hidden_challenges] ignored a malformed bdReward task 12 request\n");
		}

		auto reply = server->create_reply(this->task_id());
		reply.send();
	}
}
