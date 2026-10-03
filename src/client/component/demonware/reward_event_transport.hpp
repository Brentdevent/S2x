#pragma once

#include "game/demonware/order_progress.hpp"

namespace reward_event_relay
{
	// Called at native server queue admission and playtime boundaries
	void admit(std::uint64_t user, const demonware::order_progress::event& event, int event_class);
	void start(std::uint64_t user);
	void stop(std::uint64_t user);
	void tick();
	void reset();
}
