#include <std_include.hpp>
#include "../dw_include.hpp"
#include "../player_vote.hpp"
#include "../economy_tools.hpp"
#include "../runtime_context.hpp"
#include "component/demonware/commendations.hpp"

namespace demonware
{
	namespace
	{
		class vote_result final : public bdTaskResult
		{
		public:
			std::string bytes;

			void serialize(byte_buffer* buffer) override
			{
				buffer->write_struct(bytes.data(), static_cast<int>(bytes.size()));
			}
		};

		void send_result(service_server* server, const std::uint8_t task, const std::uint32_t error, std::string bytes = {})
		{
			auto result = std::make_unique<vote_result>();
			result->bytes = std::move(bytes);
			auto response = server->create_reply(task, error);
			response.add(result);
			response.send_struct();
		}
	}

	bdPlayerVote::bdPlayerVote() : service(243, "bdPlayerVote")
	{
		// Native SDK 0xA40180 / 0xA3FDD0 use struct tasks 243:1 / 243:2.
		register_task(1, &bdPlayerVote::vote);
		register_task(2, &bdPlayerVote::getVoteStatus);
		register_task(3, &bdPlayerVote::getVoteCounts);
	}

	void bdPlayerVote::vote(service_server* server, byte_buffer* buffer) const
	{
		std::vector<std::uint64_t> users;
		const auto giver = runtime_context::get_local_user_id();
		if (!giver || !player_vote::parse_request(buffer, users) || users.size() != 1 || users.front() == giver)
		{
			send_result(server, task_id(), BD_PARAM_PARSE_ERROR);
			return;
		}

		const auto recipient = users.front();
		const auto day = player_vote::period();
		const auto accepted = player_vote::was_given(giver, recipient, day) || commendations::give(giver, recipient, day);
		const auto saved = accepted && runtime_context::get_local_user_id() == giver && economy_tools::succeeded(player_vote::record_given(giver, recipient, day).status);
		send_result(server, task_id(), saved ? BD_NO_ERROR : BD_PLAYER_VOTE_REJECTED);
	}

	void bdPlayerVote::getVoteStatus(service_server* server, byte_buffer* buffer) const
	{
		std::vector<std::uint64_t> users;
		const auto giver = runtime_context::get_local_user_id();
		if (!giver || !player_vote::parse_request(buffer, users))
		{
			send_result(server, task_id(), BD_PARAM_PARSE_ERROR);
			return;
		}

		if (marketplace_store::get_snapshot().status != marketplace_store::store_status::ready)
		{
			send_result(server, task_id(), BD_SERVICE_NOT_AVAILABLE);
			return;
		}

		send_result(server, task_id(), BD_NO_ERROR, player_vote::vote_status(giver, users, player_vote::period()));
	}

	void bdPlayerVote::getVoteCounts(service_server* server, byte_buffer* /*buffer*/) const
	{
		// Historical account-wide totals are unavailable. The stock Commend
		// button uses getVoteStatus; do not replace totals with an empty success.
		send_result(server, task_id(), BD_SERVICE_NOT_AVAILABLE);
	}
}
