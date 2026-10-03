#pragma once

#include "servers/service_server.hpp"

namespace demonware
{
	void send_reward_push(service_server* server, std::uint32_t type, std::uint64_t user_id, const std::string& json,
		const std::string& context = "s2_steam");
}
