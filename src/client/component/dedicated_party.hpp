#pragma once

#include <string>

namespace dedicated_party
{
	struct connect_info
	{
		std::string host_address{};
		std::string key{};
		std::string session_id{};
		std::string map_name{};
		std::string gametype{};
	};

	void start();
	bool is_active();
	std::string get_current_gametype();
	bool set_next_match(const std::string& map_name, const std::string& gametype, int map_index);
	bool get_connect_info(connect_info& info);
}
