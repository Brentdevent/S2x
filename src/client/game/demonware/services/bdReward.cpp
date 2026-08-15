#include <std_include.hpp>
#include "../dw_include.hpp"

#include "component/console/console.hpp"
#include "component/hidden_challenges.hpp"

#include "steam/steam.hpp"

namespace demonware
{
	namespace
	{
		constexpr auto maximum_reported_events = 100u;
		constexpr auto maximum_reported_users = 48u;
		constexpr auto maximum_event_parameters = 10u;
		constexpr auto maximum_local_report_request_size = 64u * 1024u;
		constexpr auto maximum_remote_report_request_size = 3u * 1024u * 1024u;
		constexpr auto maximum_encryption_padding = 15u;

		class struct_buffer_reader
		{
		public:
			explicit struct_buffer_reader(const std::string_view data)
				: data_(data)
			{
			}

			bool empty() const
			{
				return data_.empty();
			}

			bool read_tag(std::uint32_t& field, std::uint8_t& wire_type)
			{
				std::uint64_t tag{};
				if (!read_varint(tag) || tag > std::numeric_limits<std::uint32_t>::max())
				{
					return false;
				}

				field = static_cast<std::uint32_t>(tag >> 3);
				wire_type = static_cast<std::uint8_t>(tag & 7);
				return field != 0;
			}

			bool read_varint(std::uint64_t& value)
			{
				value = 0;
				for (auto index = 0u; index < 10; ++index)
				{
					std::uint8_t byte{};
					if (!read_byte(byte) || (index == 9 && (byte & 0xFE) != 0))
					{
						return false;
					}

					value |= static_cast<std::uint64_t>(byte & 0x7F) << (index * 7);
					if ((byte & 0x80) == 0)
					{
						return true;
					}
				}

				return false;
			}

			bool read_length_delimited(std::string_view& value)
			{
				std::uint64_t length{};
				if (!read_varint(length) || length > data_.size())
				{
					return false;
				}

				value = data_.substr(0, static_cast<std::size_t>(length));
				data_.remove_prefix(static_cast<std::size_t>(length));
				return true;
			}

			bool skip_field(const std::uint8_t wire_type)
			{
				switch (wire_type)
				{
				case 0:
				{
					std::uint64_t value{};
					return read_varint(value);
				}
				case 1:
					return skip_bytes(8);
				case 2:
				{
					std::string_view value{};
					return read_length_delimited(value);
				}
				case 5:
					return skip_bytes(4);
				default:
					return false;
				}
			}

		private:
			bool read_byte(std::uint8_t& value)
			{
				if (data_.empty())
				{
					return false;
				}

				value = static_cast<std::uint8_t>(data_.front());
				data_.remove_prefix(1);
				return true;
			}

			bool skip_bytes(const std::size_t count)
			{
				if (count > data_.size())
				{
					return false;
				}

				data_.remove_prefix(count);
				return true;
			}

			std::string_view data_{};
		};

		struct report_reward_game_events_request
		{
			std::string context{};
			std::string transaction_id{};
			std::vector<hidden_challenges::reward_game_event> events{};
		};

		struct reward_game_event_user
		{
			std::uint64_t user_id{};
			std::string account_type{};
			std::string transaction_id{};
			std::vector<hidden_challenges::reward_game_event> events{};
		};

		struct report_reward_game_events_for_users_request
		{
			std::string context{};
			std::vector<reward_game_event_user> users{};
		};

		bool is_valid_string(const std::string_view value, const std::size_t maximum_length,
			const bool allow_empty = false)
		{
			return value.size() <= maximum_length && (allow_empty || !value.empty()) &&
				value.find('\0') == std::string_view::npos;
		}

		bool parse_event_parameter(const std::string_view data,
			hidden_challenges::reward_event_parameter& parameter)
		{
			struct_buffer_reader reader{data};
			bool has_selector{};
			bool has_value{};
			while (!reader.empty())
			{
				std::uint32_t field{};
				std::uint8_t wire_type{};
				if (!reader.read_tag(field, wire_type))
				{
					return false;
				}

				if (field == 1)
				{
					std::string_view selector{};
					if (wire_type != 2 || has_selector ||
						!reader.read_length_delimited(selector) || !is_valid_string(selector, 19))
					{
						return false;
					}

					parameter.selector.assign(selector);
					has_selector = true;
				}
				else if (field == 2)
				{
					if (wire_type != 0 || has_value || !reader.read_varint(parameter.value))
					{
						return false;
					}

					has_value = true;
				}
				else if (!reader.skip_field(wire_type))
				{
					return false;
				}
			}

			return has_selector && has_value;
		}

		bool parse_game_event(const std::string_view data,
			hidden_challenges::reward_game_event& event)
		{
			struct_buffer_reader reader{data};
			bool has_name{};
			bool has_timestamp{};
			while (!reader.empty())
			{
				std::uint32_t field{};
				std::uint8_t wire_type{};
				if (!reader.read_tag(field, wire_type))
				{
					return false;
				}

				if (field == 1)
				{
					std::string_view name{};
					if (wire_type != 2 || has_name || !reader.read_length_delimited(name) ||
						!is_valid_string(name, 99))
					{
						return false;
					}

					event.name.assign(name);
					has_name = true;
				}
				else if (field == 3)
				{
					std::uint64_t encoded{};
					if (wire_type != 0 || has_timestamp || !reader.read_varint(encoded))
					{
						return false;
					}

					const auto magnitude = static_cast<std::int64_t>(encoded >> 1);
					event.timestamp = (encoded & 1) != 0 ? -magnitude - 1 : magnitude;
					has_timestamp = true;
				}
				else if (field == 4)
				{
					std::string_view parameter_data{};
					if (wire_type != 2 || event.parameters.size() >= maximum_event_parameters ||
						!reader.read_length_delimited(parameter_data))
					{
						return false;
					}

					hidden_challenges::reward_event_parameter parameter{};
					if (!parse_event_parameter(parameter_data, parameter))
					{
						return false;
					}

					event.parameters.push_back(std::move(parameter));
				}
				else if (!reader.skip_field(wire_type))
				{
					return false;
				}
			}

			return has_name && has_timestamp;
		}

		bool parse_user_account_id(const std::string_view data, reward_game_event_user& user)
		{
			struct_buffer_reader reader{data};
			bool has_user_id{};
			bool has_account_type{};
			while (!reader.empty())
			{
				std::uint32_t field{};
				std::uint8_t wire_type{};
				if (!reader.read_tag(field, wire_type))
				{
					return false;
				}

				if (field == 1)
				{
					if (wire_type != 0 || has_user_id || !reader.read_varint(user.user_id))
					{
						return false;
					}

					has_user_id = true;
				}
				else if (field == 2)
				{
					std::string_view account_type{};
					if (wire_type != 2 || has_account_type ||
						!reader.read_length_delimited(account_type) ||
						!is_valid_string(account_type, 9))
					{
						return false;
					}

					user.account_type.assign(account_type);
					has_account_type = true;
				}
				else if (!reader.skip_field(wire_type))
				{
					return false;
				}
			}

			return has_user_id && has_account_type;
		}

		bool parse_reward_game_event_user(const std::string_view data, reward_game_event_user& user)
		{
			struct_buffer_reader reader{data};
			bool has_account{};
			bool has_transaction_id{};
			while (!reader.empty())
			{
				std::uint32_t field{};
				std::uint8_t wire_type{};
				if (!reader.read_tag(field, wire_type))
				{
					return false;
				}

				if (field == 1)
				{
					std::string_view account_data{};
					if (wire_type != 2 || has_account ||
						!reader.read_length_delimited(account_data) ||
						!parse_user_account_id(account_data, user))
					{
						return false;
					}

					has_account = true;
				}
				else if (field == 2)
				{
					std::string_view event_data{};
					if (wire_type != 2 || user.events.size() >= maximum_reported_events ||
						!reader.read_length_delimited(event_data))
					{
						return false;
					}

					hidden_challenges::reward_game_event event{};
					if (!parse_game_event(event_data, event))
					{
						return false;
					}

					user.events.push_back(std::move(event));
				}
				else if (field == 3)
				{
					std::string_view transaction_id{};
					if (wire_type != 2 || has_transaction_id ||
						!reader.read_length_delimited(transaction_id) ||
						!is_valid_string(transaction_id, 24))
					{
						return false;
					}

					user.transaction_id.assign(transaction_id);
					has_transaction_id = true;
				}
				else if (!reader.skip_field(wire_type))
				{
					return false;
				}
			}

			return has_account;
		}

		bool parse_report_reward_game_events_for_users(const std::string_view data,
			report_reward_game_events_for_users_request& request)
		{
			struct_buffer_reader reader{data};
			bool has_context{};
			while (!reader.empty())
			{
				std::uint32_t field{};
				std::uint8_t wire_type{};
				if (!reader.read_tag(field, wire_type))
				{
					return false;
				}

				if (field == 1)
				{
					std::string_view context{};
					if (wire_type != 2 || has_context || !reader.read_length_delimited(context) ||
						!is_valid_string(context, 15))
					{
						return false;
					}

					request.context.assign(context);
					has_context = true;
				}
				else if (field == 2)
				{
					std::string_view user_data{};
					if (wire_type != 2 || request.users.size() >= maximum_reported_users ||
						!reader.read_length_delimited(user_data))
					{
						return false;
					}

					reward_game_event_user user{};
					if (!parse_reward_game_event_user(user_data, user))
					{
						return false;
					}

					request.users.push_back(std::move(user));
				}
				else if (!reader.skip_field(wire_type))
				{
					return false;
				}
			}

			return has_context;
		}

		bool parse_report_reward_game_events(const std::string_view data,
			report_reward_game_events_request& request)
		{
			struct_buffer_reader reader{data};
			bool has_context{};
			bool has_transaction_id{};
			while (!reader.empty())
			{
				std::uint32_t field{};
				std::uint8_t wire_type{};
				if (!reader.read_tag(field, wire_type))
				{
					return false;
				}

				if (field == 1)
				{
					std::string_view context{};
					if (wire_type != 2 || has_context || !reader.read_length_delimited(context) ||
						!is_valid_string(context, 15))
					{
						return false;
					}

					request.context.assign(context);
					has_context = true;
				}
				else if (field == 2)
				{
					std::string_view event_data{};
					if (wire_type != 2 || request.events.size() >= maximum_reported_events ||
						!reader.read_length_delimited(event_data))
					{
						return false;
					}

					hidden_challenges::reward_game_event event{};
					if (!parse_game_event(event_data, event))
					{
						return false;
					}

					request.events.push_back(std::move(event));
				}
				else if (field == 3)
				{
					std::string_view transaction_id{};
					if (wire_type != 2 || has_transaction_id ||
						!reader.read_length_delimited(transaction_id) ||
						!is_valid_string(transaction_id, 24))
					{
						return false;
					}

					request.transaction_id.assign(transaction_id);
					has_transaction_id = true;
				}
				else if (!reader.skip_field(wire_type))
				{
					return false;
				}
			}

			return has_context;
		}

		bool read_reward_game_events_payload(byte_buffer* buffer, std::string& payload,
			const std::size_t maximum_size)
		{
			if (!buffer || !buffer->read_struct(&payload, maximum_size) || payload.size() > maximum_size)
			{
				return false;
			}

			const auto padding = buffer->get_remaining();
			return padding.size() <= maximum_encryption_padding &&
				std::ranges::all_of(padding, [](const char value)
				{
					return value == '\0';
				});
		}

		void log_task_11_zombies_event(const reward_game_event_user& user,
			const hidden_challenges::reward_game_event& event, const bool is_local_user)
		{
			if (event.name != "zombies")
			{
				return;
			}

			std::string parameters{};
			for (const auto& parameter : event.parameters)
			{
				if (!parameters.empty())
				{
					parameters.append(", ");
				}

				parameters.append(parameter.selector);
				parameters.push_back('=');
				parameters.append(std::to_string(parameter.value));
			}

			console::debug("[hidden_challenges] task 11 XUID %llu (%s, %s) zombies [%s]\n",
				static_cast<unsigned long long>(user.user_id), user.account_type.data(),
				is_local_user ? "local" : "remote", parameters.data());
		}

		void log_task_11_user_batch(const reward_game_event_user& user, const bool is_local_user)
		{
			console::debug("[hidden_challenges] task 11 XUID %llu (%s, %s): %zu events, transaction %s\n",
				static_cast<unsigned long long>(user.user_id), user.account_type.data(),
				is_local_user ? "local" : "remote", user.events.size(), user.transaction_id.data());
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
		report_reward_game_events_for_users_request request{};
		std::string payload{};
		if (read_reward_game_events_payload(buffer, payload, maximum_remote_report_request_size) &&
			parse_report_reward_game_events_for_users(payload, request))
		{
			const auto local_user_id = steam::SteamUser()->GetSteamID().bits;
			for (auto& user : request.users)
			{
				const auto is_local_user = user.user_id == local_user_id && user.account_type == "steam";
				log_task_11_user_batch(user, is_local_user);
				for (auto& event : user.events)
				{
					log_task_11_zombies_event(user, event, is_local_user);
					if (is_local_user)
					{
						hidden_challenges::submit_reward_game_event(std::move(event));
					}
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
		report_reward_game_events_request request{};
		std::string payload{};
		if (read_reward_game_events_payload(buffer, payload, maximum_local_report_request_size) &&
			parse_report_reward_game_events(payload, request))
		{
			for (auto& event : request.events)
			{
				hidden_challenges::submit_reward_game_event(std::move(event));
			}
		}
		else
		{
			console::debug("[hidden_challenges] ignored a malformed bdReward task 12 request\n");
		}

		auto reply = server->create_reply(this->task_id());
		reply.send();
	}
}
