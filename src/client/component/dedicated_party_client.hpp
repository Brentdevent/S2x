#pragma once

#include "game/game.hpp"

#include <string>

namespace utils
{
	class info_string;
}

namespace dedicated_party_client
{
	bool try_handle_join(const game::netadr_s& from, const utils::info_string& info,
		int max_players);
	bool try_handle_sync_response(const game::netadr_s& from, const utils::info_string& info,
		const std::string& challenge);
	std::string get_gametype();
	void refresh_presentation();
	void reset();
}
