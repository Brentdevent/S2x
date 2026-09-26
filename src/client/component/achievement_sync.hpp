#pragma once

namespace achievement_sync
{
	// Coalesced native fetch after a local save; retries run on the main thread.
	void request_refresh();
}
