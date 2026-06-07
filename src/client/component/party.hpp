#pragma once

#include "game/game.hpp"

namespace party
{
	game::netadr_s& get_target();

	int get_connected_client_count();
	int get_available_match_slots();
}
