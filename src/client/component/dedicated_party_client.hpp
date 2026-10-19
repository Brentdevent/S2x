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
	// Shared IP-to-native-session join transport for dedicated, custom, and HQ
	// lobbies. Dedicated games and HQs filter their session owner; only games sync rotation.
	bool try_handle_join(const game::netadr_s& from, const utils::info_string& info,
		int max_players, std::uint64_t attempt_id);
	bool try_handle_sync_response(const game::netadr_s& from, const utils::info_string& info,
		const std::string& challenge);
	bool is_pending_internal_connect(std::string_view session_id, std::uint64_t attempt_id);

	// Read-only client state used by the shared native session hooks.
	bool has_dedicated_session(game::PartyData* party_data);
	bool has_headquarters_session(game::PartyData* party_data);
	int get_joined_max_players();

	std::string get_map_name();
	std::string get_gametype();
	void refresh_presentation();
	void cancel_pending_connection();
	void commit_direct_connection();
	void reset();
}
