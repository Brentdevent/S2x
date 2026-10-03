#include <std_include.hpp>
#include "reward_event_relay.hpp"

namespace demonware::reward_event_relay
{
	namespace
	{
		constexpr std::string_view digits = "0123456789abcdef";

		void write(std::string& output, std::uint64_t value, const unsigned bytes)
		{
			for (unsigned i = 0; i < bytes; ++i, value >>= 8)
			{
				output += digits[(value >> 4) & 15];
				output += digits[value & 15];
			}
		}

		bool read(std::string_view& input, std::uint64_t& value, const unsigned bytes)
		{
			if (input.size() < bytes * 2)
			{
				return false;
			}

			value = 0;
			for (unsigned i = 0; i < bytes; ++i)
			{
				const auto high = digits.find(input[i * 2]);
				const auto low = digits.find(input[i * 2 + 1]);
				if (high == std::string_view::npos || low == std::string_view::npos)
				{
					return false;
				}

				value |= static_cast<std::uint64_t>(high * 16 + low) << (i * 8);
			}

			input.remove_prefix(bytes * 2);
			return true;
		}

		bool read_bounded(std::string_view& input, std::uint64_t& value, const unsigned bytes,
			const std::uint64_t maximum)
		{
			return read(input, value, bytes) && value <= maximum;
		}

		bool read_event(std::string_view& input, record& entry)
		{
			std::uint64_t value{};
			if (!read_bounded(input, value, 1, 1))
			{
				return false;
			}

			entry.event_class = static_cast<std::uint8_t>(value);

			if (!read_bounded(input, value, 4, INT32_MAX))
			{
				return false;
			}

			entry.event.id = static_cast<std::int32_t>(value);

			if (!read_bounded(input, value, 1, 10))
			{
				return false;
			}

			entry.event.count = static_cast<std::uint8_t>(value);

			if (!read(input, entry.event.timestamp, 8) || !read(input, entry.event.user_id, 8))
			{
				return false;
			}

			for (unsigned i = 0; i < entry.event.count; ++i)
			{
				std::uint64_t selector{};
				if (!read(input, selector, 1) || !read(input, value, 4))
				{
					return false;
				}

				entry.event.selectors[i] = static_cast<std::uint8_t>(selector);
				entry.event.values[i] = static_cast<std::uint32_t>(value);
			}

			return true;
		}
	}

	std::string encode(const std::uint64_t user, const std::uint64_t stream, const bool zombies,
		const std::span<const record> records)
	{
		if (!user || !stream || records.empty() || records.size() > batch_limit)
		{
			return {};
		}

		std::string output{};
		write(output, user, 8);
		write(output, stream, 8);
		write(output, zombies, 1);
		write(output, records.size(), 1);

		std::uint64_t previous{};
		for (const auto& entry : records)
		{
			if (entry.sequence <= previous || entry.type > operation::stop || entry.event_class > 1 ||
				entry.event.count > 10 || entry.event.id < 0)
			{
				return {};
			}

			previous = entry.sequence;
			write(output, entry.sequence, 8);
			write(output, static_cast<unsigned>(entry.type), 1);
			write(output, entry.seconds, 4);

			if (entry.type != operation::event)
			{
				continue;
			}

			write(output, entry.event_class, 1);
			write(output, entry.event.id, 4);
			write(output, entry.event.count, 1);
			write(output, entry.event.timestamp, 8);
			write(output, entry.event.user_id, 8);

			for (unsigned i = 0; i < entry.event.count; ++i)
			{
				write(output, entry.event.selectors[i], 1);
				write(output, entry.event.values[i], 4);
			}
		}

		if (output.size() > payload_limit)
		{
			return {};
		}

		return output;
	}

	std::optional<batch> decode(std::string_view payload)
	{
		if (payload.size() > payload_limit)
		{
			return {};
		}

		batch result{};
		std::uint64_t zombies{};
		std::uint64_t count{};
		if (!read(payload, result.user, 8) || !result.user ||
			!read(payload, result.stream, 8) || !result.stream ||
			!read_bounded(payload, zombies, 1, 1) ||
			!read_bounded(payload, count, 1, batch_limit) || !count)
		{
			return {};
		}

		result.zombies = zombies != 0;

		std::uint64_t previous{};
		for (std::uint64_t i = 0; i < count; ++i)
		{
			record entry{};
			std::uint64_t type{};
			std::uint64_t seconds{};
			if (!read(payload, entry.sequence, 8) || entry.sequence <= previous ||
				!read_bounded(payload, type, 1, static_cast<std::uint64_t>(operation::stop)) ||
				!read(payload, seconds, 4))
			{
				return {};
			}

			previous = entry.sequence;
			entry.type = static_cast<operation>(type);
			entry.seconds = static_cast<std::uint32_t>(seconds);

			if (entry.type == operation::event && !read_event(payload, entry))
			{
				return {};
			}

			result.records.push_back(entry);
		}

		if (!payload.empty())
		{
			return {};
		}

		return result;
	}
}
