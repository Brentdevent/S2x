#pragma once

#include <string>
#include <string_view>

namespace upnp
{
	// Main-thread snapshot for this hosted session: manual override or a live UPnP mapping.
	std::string get_endpoint(std::string_view session_id);
	void shutdown();
}
