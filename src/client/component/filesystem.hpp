#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace filesystem
{
	// Observers of config files read by the exec command (packaged or loose).
	void on_exec_file_read(const std::function<void(const std::string& name, const std::string& data)>& callback);

	// Lets a component rewrite a config's text before the engine executes it:
	// loose files, and packaged rawfiles the engine copied into the exec
	// buffer. `capacity` is the size of that buffer including the terminator;
	// the callback returns true when it changed the text and must keep the
	// result below that capacity (a longer result is discarded).
	void on_exec_file_transform(
		const std::function<bool(const std::string& name, std::string& data, std::size_t capacity)>& callback);

	std::string read_file(const std::string& path);

	bool read_file(
		const std::string& path,
		std::string* data,
		std::string* real_path = nullptr
	);

	bool find_file(
		const std::string& path,
		std::string* real_path
	);

	bool exists(const std::string& path);

	void register_path(const std::filesystem::path& path);
	void unregister_path(const std::filesystem::path& path);

	std::vector<std::string> get_search_paths();
	std::vector<std::string> get_search_paths_rev();
}
