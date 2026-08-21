#pragma once

namespace updater
{
	void update();
	void update_store_runtime(const std::filesystem::path& base, const std::string& required_file);
}
