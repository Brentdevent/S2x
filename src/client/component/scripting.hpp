#pragma once

namespace scripting
{
	extern std::unordered_map<std::string, std::unordered_map<std::string, const char*>> script_function_table;
	extern std::unordered_map<std::string, std::vector<std::pair<std::string, const char*>>> script_function_table_sort;
	extern std::unordered_map<const char*, std::pair<std::string, std::string>> script_function_table_rev;

	extern std::string current_file;

	void on_shutdown(const std::function<void(int)>& callback);
	// Runs after script teardown, excluding fast restarts.
	void on_scripts_free(const std::function<void()>& callback);
	void on_init(const std::function<void()>& callback);

	std::optional<std::string> get_canonical_string(unsigned int id);
	std::string get_token(unsigned int id);
}
