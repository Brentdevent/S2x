#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace dedicated_settings::detail
{
	// Native bool conversion (RVA B3CFA) is atoi(text) != 0, not a true/false
	// keyword parser. Mirror its decimal prefix within the signed 32-bit range.
	// Never invoke atoi on overflow, or guess its implementation-specific result.
	// B2910 copies through a 0x400-byte buffer first; do not guess truncation.
	inline std::optional<bool> parse_bool_value(std::string_view text)
	{
		if (text.size() >= 0x400)
		{
			return std::nullopt;
		}

		const auto is_space = [](const char c)
		{
			return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
		};
		while (!text.empty() && is_space(text.front()))
		{
			text.remove_prefix(1);
		}

		const auto negative = !text.empty() && text.front() == '-';
		if (!text.empty() && (text.front() == '+' || negative))
		{
			text.remove_prefix(1);
		}

		const std::uint32_t limit = negative ? 2147483648u : 2147483647u;
		std::uint32_t magnitude = 0;
		for (const auto c : text)
		{
			if (c < '0' || c > '9')
			{
				break;
			}
			const auto digit = static_cast<std::uint32_t>(c - '0');
			if (magnitude > (limit - digit) / 10)
			{
				return std::nullopt;
			}
			magnitude = magnitude * 10 + digit;
		}
		return magnitude != 0;
	}
}
