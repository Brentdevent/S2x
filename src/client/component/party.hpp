#pragma once

#include "game/game.hpp"

namespace party
{
	game::netadr_s& get_target();

	bool dedicated_private_party_ready();

	int get_connected_client_count();
	int get_available_match_slots();
}
