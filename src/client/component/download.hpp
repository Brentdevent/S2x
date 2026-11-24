#pragma once

#include "game/game.hpp"

#include <utils/info_string.hpp>

#include <cstdint>
#include <string>

// Downloads server content (usermaps) over HTTP from sv_wwwBaseUrl
namespace download
{
	// Server: true for a map in a usermaps folder; starts hashing its files
	bool is_server_usermap(const std::string& map_name);

	// Client: true when it takes over the connect attempt to check the usermap
	bool begin_connect_check(const game::netadr_s& from, const utils::info_string& info,
		int max_clients, std::uint64_t attempt_id);

	// Client: reconnects through the download path if the next rotation map is missing
	void check_rotation_map(const game::netadr_s& from, const utils::info_string& info);
}
