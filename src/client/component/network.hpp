#pragma once

#include "game/game.hpp"

namespace network
{
	using callback = std::function<void(const game::netadr_s&, const std::string_view&)>;

	void on(const std::string& command, const callback& callback);

	void send(
		const game::netadr_s& address,
		const std::string& command,
		const std::string& data = {},
		char separator = ' '
	);

	void send_data(const game::netadr_s& address, const std::string_view& data);

	bool are_addresses_equal(const game::netadr_s& a, const game::netadr_s& b);

	const char* net_adr_to_string(const game::netadr_s& address);
}

inline bool operator==(const game::netadr_s& a, const game::netadr_s& b)
{
	return network::are_addresses_equal(a, b);
}

inline bool operator!=(const game::netadr_s& a, const game::netadr_s& b)
{
	return !(a == b);
}

namespace std
{
	template <>
	struct equal_to<game::netadr_s>
	{
		using result_type = bool;

		bool operator()(const game::netadr_s& lhs, const game::netadr_s& rhs) const
		{
			return network::are_addresses_equal(lhs, rhs);
		}
	};

	template <>
	struct hash<game::netadr_s>
	{
		size_t operator()(const game::netadr_s& address) const noexcept
		{
			const auto type_hash = hash<int>()(static_cast<int>(address.type));

			if (address.type != game::NA_IP && address.type != game::NA_BROADCAST)
			{
				return type_hash;
			}

			const auto ip_hash = hash<std::uint32_t>()(address.addr);
			const auto port_hash = hash<std::uint16_t>()(address.port);

			return type_hash ^ ip_hash ^ port_hash;
		}
	};
}
