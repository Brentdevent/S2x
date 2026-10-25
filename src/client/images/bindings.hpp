#pragma once

#include <cstdint>
#include <string>

namespace images::bindings
{
	const char* role_name(std::uint32_t semantic_hash);
	std::string format(unsigned int slot, std::uint32_t semantic_hash, const char* image);
}
