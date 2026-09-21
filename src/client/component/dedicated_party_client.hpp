#pragma once

#include "game/game.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace utils
{
	class info_string;
}

namespace dedicated_party_client
{
	// Shared IP-to-native-session join transport. Custom listen lobbies skip
	// all dedicated-owner/capacity/rotation presentation once the join is set up.
	bool try_handle_join(const game::netadr_s& from, const utils::info_string& info,
		int max_players, std::uint64_t attempt_id);
	bool try_handle_sync_response(const game::netadr_s& from, const utils::info_string& info,
		const std::string& challenge);
	bool is_pending_internal_connect(std::string_view session_id, std::uint64_t attempt_id);
	std::string get_map_name();
	std::string get_gametype();
	void refresh_presentation();
	void cancel_pending_connection();
	void commit_direct_connection();
	void reset();
}
