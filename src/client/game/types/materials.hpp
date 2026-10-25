#pragma once

#include "images.hpp"

namespace game
{
	struct MaterialImageBinding
	{
		std::uint32_t semantic_hash;
		std::uint32_t unknown;
		GfxImage* image;
	};

	static_assert(sizeof(MaterialImageBinding) == 0x10);
	static_assert(offsetof(MaterialImageBinding, image) == 8);

	// Partial layout; only the name is mapped.
	struct MaterialTechniqueSet
	{
		const char* name;
	};

	static_assert(offsetof(MaterialTechniqueSet, name) == 0);
	static_assert(sizeof(MaterialTechniqueSet) == 8);

	// Partial layout, verified against Greyhound and live SP/MP asset enumeration.
	struct Material
	{
		const char* name;
		std::uint8_t unknown08[0x9A];
		std::uint8_t image_count;
		std::uint8_t unknownA3[0x15];
		const MaterialTechniqueSet* technique_set;
		MaterialImageBinding* image_table;
	};

	static_assert(offsetof(Material, name) == 0);
	static_assert(offsetof(Material, image_count) == 0xA2);
	static_assert(offsetof(Material, technique_set) == 0xB8);
	static_assert(offsetof(Material, image_table) == 0xC0);
	static_assert(sizeof(Material) == 0xC8);
}
