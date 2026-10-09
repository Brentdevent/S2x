#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace demonware
{
	class byte_buffer;

	namespace reward_game_events
	{
		struct parameter
		{
			std::string selector{};
			std::uint64_t value{};
		};

		struct event
		{
			std::string name{};
			std::int64_t timestamp{};
			std::vector<parameter> parameters{};
		};

		struct user_event_batch
		{
			std::uint64_t user_id{};
			std::string account_type{};
			std::string transaction_id{};
			std::vector<event> events{};
		};

		struct report_request
		{
			std::string context{};
			std::string transaction_id{};
			std::vector<event> events{};
		};

		struct report_for_users_request
		{
			std::string context{};
			std::vector<user_event_batch> users{};
		};

		bool parse_report_request(byte_buffer* buffer, report_request& request);
		bool parse_report_for_users_request(byte_buffer* buffer,
			report_for_users_request& request);
	}
}
