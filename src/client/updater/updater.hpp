#pragma once

#include "update_cancelled.hpp"

namespace updater
{
	void cleanup();
	void run(const std::filesystem::path& base);
	void run_store_runtime_update(const std::filesystem::path& base, const std::string& required_file);
}
