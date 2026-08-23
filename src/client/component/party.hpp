#pragma once

#include "game/game.hpp"

#include <cstdint>
#include <string>

namespace party
{
	struct internal_connect_request
	{
		std::uint64_t attempt_id{};
		std::string host_address{};
		std::string key{};
		std::string session_id{};
		std::string map_name{};
		std::string gametype{};
	};

	game::netadr_s& get_target();
	bool is_connection_attempt_current(std::uint64_t attempt_id);
	void cancel_pending_connection();
	void queue_connect(std::string address);
	void execute_internal_connect(const internal_connect_request& request);

	bool resolve_map_index(const std::string& map_name, int& map_index);
	bool validate_gametype(const std::string& gametype);
	bool set_match_rules_gametype(const std::string& gametype);
	void apply_map_settings(const std::string& map_name, const std::string& gametype, int map_index);
	std::string loaded_map_name();
	std::string loaded_gametype();
	bool server_running();

	int get_connected_client_count();
	int get_available_match_slots();
}
