#include <std_include.hpp>

#include "reward_game_event.hpp"
#include "dw_include.hpp"

namespace demonware::reward_game_events
{
	namespace
	{
		constexpr auto maximum_reported_events = 100u;
		constexpr auto maximum_reported_users = 48u;
		constexpr auto maximum_event_parameters = 10u;
		constexpr auto maximum_report_request_size = 64u * 1024u;
		// Task 11 can contain 48 user batches with up to 100 events each.
		constexpr auto maximum_report_for_users_request_size = 3u * 1024u * 1024u;
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

		bool is_valid_string(const std::string_view value, const std::size_t maximum_length)
		{
			return !value.empty() && value.size() <= maximum_length &&
				value.find('\0') == std::string_view::npos;
		}

		bool parse_parameter(const std::string_view data, parameter& result)
		{
			// RewardGameEventValue: 1 = selector, 2 = uint64 value.
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

					result.selector.assign(selector);
					has_selector = true;
				}
				else if (field == 2)
				{
					if (wire_type != 0 || has_value || !reader.read_varint(result.value))
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

		bool parse_event(const std::string_view data, event& result)
		{
			// RewardGameEvent: 1 = name, 3 = ZigZag timestamp, 4 = repeated values.
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

					result.name.assign(name);
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
					result.timestamp = (encoded & 1) != 0 ? -magnitude - 1 : magnitude;
					has_timestamp = true;
				}
				else if (field == 4)
				{
					std::string_view parameter_data{};
					if (wire_type != 2 || result.parameters.size() >= maximum_event_parameters ||
						!reader.read_length_delimited(parameter_data))
					{
						return false;
					}

					parameter value{};
					if (!parse_parameter(parameter_data, value))
					{
						return false;
					}

					result.parameters.push_back(std::move(value));
				}
				else if (!reader.skip_field(wire_type))
				{
					return false;
				}
			}

			return has_name && has_timestamp;
		}

		bool parse_user_account(const std::string_view data, user_event_batch& result)
		{
			// UserAccountID: 1 = user ID, 2 = account type.
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
					if (wire_type != 0 || has_user_id || !reader.read_varint(result.user_id))
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

					result.account_type.assign(account_type);
					has_account_type = true;
				}
				else if (!reader.skip_field(wire_type))
				{
					return false;
				}
			}

			return has_user_id && has_account_type;
		}

		bool parse_user_event_batch(const std::string_view data, user_event_batch& result)
		{
			// UserEventBatch: 1 = account, 2 = repeated events, 3 = transaction ID.
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
						!parse_user_account(account_data, result))
					{
						return false;
					}

					has_account = true;
				}
				else if (field == 2)
				{
					std::string_view event_data{};
					if (wire_type != 2 || result.events.size() >= maximum_reported_events ||
						!reader.read_length_delimited(event_data))
					{
						return false;
					}

					event value{};
					if (!parse_event(event_data, value))
					{
						return false;
					}

					result.events.push_back(std::move(value));
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

					has_transaction_id = true;
				}
				else if (!reader.skip_field(wire_type))
				{
					return false;
				}
			}

			return has_account;
		}

		bool parse_report_payload(const std::string_view data, std::vector<event>& events)
		{
			// ReportRewardGameEventsRequest: 1 = context, 2 = repeated events,
			// 3 = transaction ID.
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

					has_context = true;
				}
				else if (field == 2)
				{
					std::string_view event_data{};
					if (wire_type != 2 || events.size() >= maximum_reported_events ||
						!reader.read_length_delimited(event_data))
					{
						return false;
					}

					event value{};
					if (!parse_event(event_data, value))
					{
						return false;
					}

					events.push_back(std::move(value));
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

					has_transaction_id = true;
				}
				else if (!reader.skip_field(wire_type))
				{
					return false;
				}
			}

			return has_context;
		}

		bool parse_report_for_users_payload(const std::string_view data,
			std::vector<user_event_batch>& users)
		{
			// ReportRewardGameEventsForUsersRequest: 1 = context, 2 = repeated users.
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

					has_context = true;
				}
				else if (field == 2)
				{
					std::string_view user_data{};
					if (wire_type != 2 || users.size() >= maximum_reported_users ||
						!reader.read_length_delimited(user_data))
					{
						return false;
					}

					user_event_batch user{};
					if (!parse_user_event_batch(user_data, user))
					{
						return false;
					}

					users.push_back(std::move(user));
				}
				else if (!reader.skip_field(wire_type))
				{
					return false;
				}
			}

			return has_context;
		}

		bool read_payload(byte_buffer* buffer, std::string& payload, const std::size_t maximum_size)
		{
			// Lobby tasks wrap StructBuffer in a typed bdByteBuffer structured-data value.
			// Decryption can leave up to one AES block of zero padding after that value.
			if (!buffer || !buffer->read_struct(&payload, maximum_size))
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
	}

	bool parse_report_request(byte_buffer* buffer, std::vector<event>& events)
	{
		std::string payload{};
		std::vector<event> parsed{};
		if (!read_payload(buffer, payload, maximum_report_request_size) ||
			!parse_report_payload(payload, parsed))
		{
			return false;
		}

		events = std::move(parsed);
		return true;
	}

	bool parse_report_for_users_request(byte_buffer* buffer,
		std::vector<user_event_batch>& users)
	{
		std::string payload{};
		std::vector<user_event_batch> parsed{};
		if (!read_payload(buffer, payload, maximum_report_for_users_request_size) ||
			!parse_report_for_users_payload(payload, parsed))
		{
			return false;
		}

		users = std::move(parsed);
		return true;
	}
}
