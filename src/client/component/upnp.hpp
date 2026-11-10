#pragma once

#include <string>
#include <string_view>

namespace upnp
{
	// Main-thread snapshot; empty until this specific hosted session has a mapping.
	std::string get_endpoint(std::string_view session_id);
	void shutdown();
}
