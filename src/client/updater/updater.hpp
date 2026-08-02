#pragma once

#include "update_cancelled.hpp"

namespace updater
{
	void cleanup();
	void run(const std::filesystem::path& base);
}
