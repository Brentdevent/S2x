#pragma once

#include <array>
#include <string>
#include <string_view>

namespace upnp::protocol
{
	bool read_http_body(std::string_view input, std::string& body, bool allow_fault = false);
	bool read_mapping_response(std::string_view body, std::array<std::string, 3>& fields);
}
