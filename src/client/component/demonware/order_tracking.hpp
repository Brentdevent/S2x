#pragma once

#include "game/demonware/reward/event_relay.hpp"

namespace order_progress
{
	// Main thread, for the owning local XUID
	std::optional<std::uint64_t> receive(const demonware::reward_event_relay::batch& batch);
	void disconnect();
}
