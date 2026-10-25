#pragma once

#include <optional>
#include <string>

namespace images::files
{
	void refresh();
	std::optional<std::string> find(const std::string& name);
}
