#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace achievement_sync
{
	// Main-thread publication for local-player achievement requests on DW workers.
	std::uint64_t local_user_id();

	// Coalesced native fetch after a local save; retries run on the main thread.
	void request_refresh();

	// Refresh saved counters without clearing and rebuilding the full native cache.
	void request_progress_refresh(std::vector<std::string> names);
}
