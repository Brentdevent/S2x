#pragma once

#include "game/demonware/reward_event_relay.hpp"

namespace order_progress
{
	// Main thread, for the owning local XUID
	void receive(const demonware::reward_event_relay::batch& batch);
	void disconnect();
}
