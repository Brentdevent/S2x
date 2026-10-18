#pragma once

#include <optional>

namespace pause::process
{
	// Returns an exit code in helper mode, or no value during normal startup.
	std::optional<int> run_helper();

	bool start();
	void stop();
}
