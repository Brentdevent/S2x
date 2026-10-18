#pragma once

#include "party_session.hpp"

namespace headquarters
{
	// Native lobby type, distinct from the advertised party_session value (3).
	inline constexpr int party_type = 2;

	// Native HQ allocation: 47 remote players plus the local HQ owner.
	inline constexpr int session_capacity = 48;
	inline constexpr int player_capacity = session_capacity - 1;
	inline constexpr auto map_name = "mp_hub_allies";
	inline constexpr auto gametype = "hub";

	bool is_host();
	void start();
	bool get_connect_info(party::session::connect_info& info);
}
