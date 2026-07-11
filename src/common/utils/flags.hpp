#pragma once

#include <string>
#include <optional>

namespace utils::flags
{
	bool has_flag(const std::string& flag);
	std::optional<std::string> get_value(const std::string& flag);
	std::optional<std::string> get_plus_value(const std::string& command);
	std::optional<std::string> get_set_value(const std::string& dvar);
}
