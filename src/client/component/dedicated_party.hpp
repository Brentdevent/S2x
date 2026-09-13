#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dedicated_party
{
	// A hosted dedicated lobby retains its frontend owner as a party/session
	// member, but that owner never becomes a gameplay client.
	constexpr int get_member_capacity(const int player_capacity)
	{
		return player_capacity + 1;
	}

	// Keep the owner beyond every gameplay index, including when a server changes
	// its configured limit between maps. Native sessions must include this index.
	int get_host_member_index();
	int get_session_capacity();
	// Applied human limit, or -1 before the dedicated lobby exists.
	int get_max_players();

	struct dedicated_match_t
	{
		std::string map_name{};
		std::string gametype{};
		int map_index{};
	};

	struct connect_info
	{
		std::string host_address{};
		std::string key{};
		std::string session_id{};
		std::string map_name{};
		std::string gametype{};
		std::uint64_t match_sequence{};
		int member_count{};
		int max_members{};
		bool match_running{};
	};

	void start();
	bool is_active();
	std::string get_current_gametype();
	bool set_rotation(std::vector<dedicated_match_t> rotation);
	bool rotate();
	bool set_next_match(const std::string& map_name, const std::string& gametype, int map_index);
	bool get_connect_info(connect_info& info);
}
