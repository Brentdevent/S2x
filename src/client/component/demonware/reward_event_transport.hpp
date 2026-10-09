#pragma once

#include "game/demonware/achievement/order_progress.hpp"

namespace reward_event_relay
{
	struct delivery
	{
		std::uint64_t stream{};
		std::uint64_t sequence{};
	};

	// Retain the native occurrence until the owning client accepts its delivery.
	delivery admit(std::uint64_t user, const demonware::order_progress::event& event, int event_class);
	bool is_pending(std::uint64_t user, const delivery& value);
	void start(std::uint64_t user);
	void stop(std::uint64_t user);
	void tick();
	void reset();
}
