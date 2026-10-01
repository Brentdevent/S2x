#include <std_include.hpp>
#include "reward_event_relay.hpp"

namespace demonware::reward_event_relay
{
	namespace
	{
		constexpr std::string_view digits = "0123456789abcdef";
		void append_integer(std::string& out, std::uint64_t value, const unsigned bytes)
		{
			for (unsigned i = 0; i < bytes; ++i, value >>= 8)
			{
				out += digits[(value >> 4) & 15];
				out += digits[value & 15];
			}
		}
		bool read_integer(std::string_view& input, std::uint64_t& value, const unsigned bytes)
		{
			if (input.size() < bytes * 2)
			{
				return false;
			}
			value = 0;
			for (unsigned i = 0; i < bytes; ++i)
			{
				const auto hi = digits.find(input[i * 2]), lo = digits.find(input[i * 2 + 1]);
				if (hi == digits.npos || lo == digits.npos)
				{
					return false;
				}
				value |= static_cast<std::uint64_t>(hi * 16 + lo) << (i * 8);
			}
			input.remove_prefix(bytes * 2);
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
		std::string out;
		append_integer(out, user, 8);
		append_integer(out, stream, 8);
		append_integer(out, zombies, 1);
		append_integer(out, records.size(), 1);
		std::uint64_t previous{};
		for (const auto& value : records)
		{
			if (!value.sequence || value.sequence <= previous || value.type > operation::stop ||
				value.event_class > 1 || value.event.count > 10 || value.event.id < 0)
			{
				return {};
			}
			previous = value.sequence;
			append_integer(out, value.sequence, 8);
			append_integer(out, static_cast<unsigned>(value.type), 1);
			append_integer(out, value.seconds, 4);
			if (value.type != operation::event)
			{
				continue;
			}
			append_integer(out, value.event_class, 1);
			append_integer(out, value.event.id, 4);
			append_integer(out, value.event.count, 1);
			append_integer(out, value.event.timestamp, 8);
			append_integer(out, value.event.user_id, 8);
			for (unsigned i = 0; i < value.event.count; ++i)
			{
				append_integer(out, value.event.selectors[i], 1);
				append_integer(out, value.event.values[i], 4);
			}
		}
		return out.size() <= payload_limit ? out : std::string{};
	}

	std::optional<batch> decode(std::string_view payload)
	{
		if (payload.size() > payload_limit)
		{
			return {};
		}
		batch result;
		std::uint64_t value{}, count{};
		if (!read_integer(payload, result.user, 8) || !result.user || !read_integer(payload, result.stream, 8) || !result.stream ||
			!read_integer(payload, value, 1) || value > 1 || !read_integer(payload, count, 1) || !count || count > batch_limit)
		{
			return {};
		}
		result.zombies = value != 0;
		std::uint64_t previous{};
		for (unsigned n = 0; n < count; ++n)
		{
			record entry;
			if (!read_integer(payload, entry.sequence, 8) || entry.sequence <= previous ||
				!read_integer(payload, value, 1) || value > static_cast<unsigned>(operation::stop))
			{
				return {};
			}
			previous = entry.sequence;
			entry.type = static_cast<operation>(value);
			if (!read_integer(payload, value, 4))
			{
				return {};
			}
			entry.seconds = static_cast<std::uint32_t>(value);
			if (entry.type == operation::event)
			{
				if (!read_integer(payload, value, 1) || value > 1)
				{
					return {};
				}
				entry.event_class = static_cast<std::uint8_t>(value);
				if (!read_integer(payload, value, 4) || value > INT32_MAX)
				{
					return {};
				}
				entry.event.id = static_cast<std::int32_t>(value);
				if (!read_integer(payload, value, 1) || value > 10)
				{
					return {};
				}
				entry.event.count = static_cast<std::uint8_t>(value);
				if (!read_integer(payload, entry.event.timestamp, 8) || !read_integer(payload, entry.event.user_id, 8))
				{
					return {};
				}
				for (unsigned i = 0; i < entry.event.count; ++i)
				{
					if (!read_integer(payload, value, 1))
					{
						return {};
					}
					entry.event.selectors[i] = static_cast<std::uint8_t>(value);
					if (!read_integer(payload, value, 4))
					{
						return {};
					}
					entry.event.values[i] = static_cast<std::uint32_t>(value);
				}
			}
			result.records.push_back(entry);
		}
		return payload.empty() ? std::optional{std::move(result)} : std::nullopt;
	}
}
