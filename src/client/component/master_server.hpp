#pragma once

#include "game/game.hpp"

namespace master_server
{
	inline constexpr auto game_name = "S2";

	bool is_enabled();
	bool get_address(game::netadr_s& address);
	bool is_master_address(const game::netadr_s& address);
	bool handle_incoming_packet(
		const char* data,
		int size,
		std::uint32_t source_address,
		std::uint16_t source_port
	);
}
