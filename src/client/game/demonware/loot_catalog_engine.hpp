#pragma once

#include "loot_catalog.hpp"

namespace demonware::loot_catalog_engine
{
	struct database_state
	{
		bool transient_waiting{};
		bool postload_complete{};
		bool completion_event_signaled{};
		bool operator==(const database_state&) const = default;
	};

	// Called after the scheduler's original stock postload function. The event
	// signals DB work completion; the separate bit confirms main-thread postload.
	inline bool ready_for_snapshot(const database_state& state)
	{
		return !state.transient_waiting && state.postload_complete && state.completion_event_signaled;
	}

	// Acquire and resolve one DB generation directly into owned catalog facts.
	// All bounded cell views remain local to this engine-thread-only build.
	std::optional<loot_catalog::catalog> build_catalog(
		std::uint64_t generation, std::uint32_t producer_thread_id);
}
