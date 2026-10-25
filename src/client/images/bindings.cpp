#include <std_include.hpp>

#include "bindings.hpp"
#include <utils/string.hpp>

namespace images::bindings
{
	const char* role_name(const std::uint32_t semantic_hash)
	{
		// Verified roles from Greyhound's GameWorldWar2::ReadXMaterial.
		switch (semantic_hash)
		{
		case 0xA0AB1041:
			return "Color";
		case 0x59D30D0F:
			return "Normal";
		case 0x34ECCCB3:
			return "Specular";
		default:
			return nullptr;
		}
	}

	std::string format(const unsigned int slot, const std::uint32_t semantic_hash, const char* image)
	{
		const auto* role = role_name(semantic_hash);
		if (!role)
		{
			return utils::string::va("[%u] %08X %s", slot, semantic_hash, image);
		}

		return utils::string::va("[%u] %s (%08X): %s", slot, role, semantic_hash, image);
	}
}
