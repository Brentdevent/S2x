#pragma once

#include <string>
#include <optional>
#include <utility>
#include <vector>

namespace utils::flags
{
	void add_flag(const std::string& flag);
	bool has_flag(const std::string& flag);
	std::optional<std::string> get_value(const std::string& flag);
	std::optional<std::string> get_plus_value(const std::string& command);
	// First matching assignment, lowercased; an explicit empty value is present.
	std::optional<std::string> get_set_value(const std::string& dvar);

	// Every "+set <dvar> <value>" pair on the command line, in order. Dvar
	// names are lowercased, values keep their original case (including empties).
	std::vector<std::pair<std::string, std::string>> get_set_values();
}
