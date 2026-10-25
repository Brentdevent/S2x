#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace images::package
{
	// WWII image PAK chunk: total size, flags, independent LZ4 blocks, 4-byte padding.
	// The caller supplies the expected size from the verified image stream layout.
	std::string decode(std::string_view bytes, std::size_t expected_size);
}
