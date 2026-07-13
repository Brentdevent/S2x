#pragma once

#include "game/game.hpp"

namespace party
{
	game::netadr_s& get_target();

	bool resolve_map_index(const std::string& map_name, int& map_index);
	bool validate_gametype(const std::string& gametype);
	void apply_map_settings(const std::string& map_name, const std::string& gametype, int map_index);
	std::string loaded_map_name();
	std::string loaded_gametype();
	bool server_running();

	int get_connected_client_count();
	int get_available_match_slots();
}
