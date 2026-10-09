#pragma once

#include "catalog.hpp"

namespace demonware::loot_catalog_engine
{
	struct database_state
	{
		bool transient_waiting{};
		bool postload_complete{};
		bool completion_event_signaled{};

		bool operator==(const database_state&) const = default;
	};

	inline bool ready_for_snapshot(const database_state& state)
	{
		return !state.transient_waiting && state.postload_complete && state.completion_event_signaled;
	}

	std::optional<loot_catalog::catalog> build_catalog(std::uint64_t generation, std::uint32_t producer_thread_id);
}
