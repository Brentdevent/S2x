#pragma once

#include "component/command.hpp"

namespace commendations
{
	// Called by the local DW service thread. Native networking/UI stays on main.
	bool give(std::uint64_t giver, std::uint64_t recipient, std::uint32_t day);
	bool receive(unsigned local_client, const command::params& args);
	void disconnect();
}
