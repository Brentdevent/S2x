#pragma once

#include "game/game.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace utils
{
	class info_string;
}

namespace party_client
{
	// Shared IP-to-native-session join transport for dedicated, custom, HQ, and social
	// parties. Dedicated games and HQs filter their session owner; only games sync rotation.
	bool try_handle_join(const game::netadr_s& from, const utils::info_string& info,
		int max_players, std::uint64_t attempt_id, bool friend_join = false, std::uint32_t discord_token = 0,
		bool custom_invite = false, bool follow_party = false);
	bool try_handle_sync_response(const game::netadr_s& from, const utils::info_string& info,
		const std::string& challenge);
	bool is_party_follow_current(std::uint64_t attempt_id);
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
