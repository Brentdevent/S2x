#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace party::invite
{
	constexpr std::uint64_t lifetime_seconds = 300;
	constexpr std::uint64_t clock_skew_seconds = 30;
	constexpr std::size_t max_secret_length = 127;
	constexpr std::string_view steam_connect_prefix = "-s2x-invite ";

	template <typename T>
	bool parse_number(const std::string_view text, T& value)
	{
		if (text.empty())
		{
			return false;
		}

		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);

		return error == std::errc{} && end == text.data() + text.size();
	}

	inline bool public_ipv4(const std::string_view address)
	{
		std::array<unsigned, 4> octets{};
		auto remaining = address;

		for (std::size_t i = 0; i < octets.size(); ++i)
		{
			const auto dot = remaining.find('.');
			const auto last_octet = i + 1 == octets.size();

			if (!last_octet && dot == std::string_view::npos)
			{
				return false;
			}

			if (last_octet && dot != std::string_view::npos)
			{
				return false;
			}

			const auto part = remaining.substr(0, dot);

			if (part.size() > 3 || (part.size() > 1 && part[0] == '0')
				|| !parse_number(part, octets[i]) || octets[i] > 255)
			{
				return false;
			}

			if (!last_octet)
			{
				remaining.remove_prefix(dot + 1);
			}
		}

		const auto first_octet = octets[0];
		const auto second_octet = octets[1];

		return first_octet != 0 && first_octet != 10 && first_octet != 127 && first_octet < 224
			&& !(first_octet == 100 && second_octet >= 64 && second_octet <= 127)
			&& !(first_octet == 169 && second_octet == 254)
			&& !(first_octet == 172 && second_octet >= 16 && second_octet <= 31)
			&& !(first_octet == 192 && second_octet == 168)
			&& !(first_octet == 198 && (second_octet == 18 || second_octet == 19));
	}

	inline bool valid_endpoint(const std::string_view endpoint)
	{
		const auto colon = endpoint.find(':');
		std::uint16_t port{};

		return colon != std::string_view::npos && public_ipv4(endpoint.substr(0, colon))
			&& parse_number(endpoint.substr(colon + 1), port) && port != 0;
	}

	inline bool valid_session(const std::string_view id)
	{
		return id.size() == 16 && id != "0000000000000000"
			&& std::all_of(id.begin(), id.end(), [](const char c)
			{
				return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
			});
	}

	struct ticket
	{
		std::string mode;
		std::string endpoint;
		std::string session_id;
		std::uint64_t expires{};
		std::uint32_t discord_token{};
	};

	inline std::optional<ticket> parse(const std::string_view secret, const std::uint64_t now)
	{
		if (secret.size() > max_secret_length)
		{
			return {};
		}

		const auto discord = secret.starts_with("s2x2|");
		std::array<std::string_view, 6> fields{};
		const std::size_t field_count = discord ? 6 : 5;
		auto remaining = secret;

		for (std::size_t i = 0; i < field_count; ++i)
		{
			const auto separator = remaining.find('|');
			const auto last_field = i + 1 == field_count;

			if (!last_field && separator == std::string_view::npos)
			{
				return {};
			}

			if (last_field && separator != std::string_view::npos)
			{
				return {};
			}

			fields[i] = remaining.substr(0, separator);

			if (!last_field)
			{
				remaining.remove_prefix(separator + 1);
			}
		}

		const auto& [version, mode, endpoint, session_id, expiry, token] = fields;
		std::uint64_t expires{};
		std::uint32_t discord_token{};

		if ((!discord && version != "s2x1") || (mode != "mp" && mode != "zm")
			|| !valid_endpoint(endpoint) || !valid_session(session_id)
			|| !parse_number(expiry, expires) || expires <= now
			|| expires - now > lifetime_seconds + clock_skew_seconds
			|| (discord && (!parse_number(token, discord_token) || !discord_token)))
		{
			return {};
		}

		return ticket{std::string{mode}, std::string{endpoint}, std::string{session_id}, expires, discord_token};
	}

	inline std::string encode(const ticket& value)
	{
		const auto version = value.discord_token ? "s2x2|" : "s2x1|";
		auto secret = version + value.mode + "|" + value.endpoint + "|" + value.session_id
			+ "|" + std::to_string(value.expires);

		if (value.discord_token)
		{
			secret += "|" + std::to_string(value.discord_token);
		}

		return secret;
	}

	inline std::optional<ticket> parse_steam_connect(const std::string_view value, const std::uint64_t now)
	{
		if (!value.starts_with(steam_connect_prefix))
		{
			return {};
		}

		const auto ticket = parse(value.substr(steam_connect_prefix.size()), now);
		return ticket && !ticket->discord_token ? ticket : std::nullopt;
	}
}
