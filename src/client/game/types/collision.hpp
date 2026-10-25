#pragma once

#include <cstddef>
#include <cstdint>

namespace game
{
	// Shared SP/MP layout: CM_BoxTrace (MP 0x647990 / SP 0x462770)
	// initializes exactly 0x40 bytes in both supported binaries.
	struct Trace
	{
		float fraction;
		float normal[3];
		std::uint32_t surface_flags;
		std::uint8_t unknown14[4];
		std::uint64_t contents;
		std::uint32_t hit_type;
		std::uint16_t hit_id;
		std::uint8_t unknown26[12];
		std::uint8_t all_solid;
		std::uint8_t start_solid;
		std::uint8_t unknown34[12];
	};

	static_assert(sizeof(Trace) == 0x40);
	static_assert(offsetof(Trace, surface_flags) == 0x10);
	static_assert(offsetof(Trace, contents) == 0x18);
	static_assert(offsetof(Trace, hit_type) == 0x20);
	static_assert(offsetof(Trace, hit_id) == 0x24);
	static_assert(offsetof(Trace, all_solid) == 0x32);
	static_assert(offsetof(Trace, start_solid) == 0x33);

	struct CollisionMaterial
	{
		const char* name;
		std::uint32_t surface_flags;
		std::uint32_t unknown0C;
		std::uint64_t contents;
	};

	static_assert(sizeof(CollisionMaterial) == 0x18);
	static_assert(offsetof(CollisionMaterial, surface_flags) == 8);
	static_assert(offsetof(CollisionMaterial, contents) == 0x10);

	// Partial collision-world layout through the material table.
	struct clipMap_t
	{
		std::uint8_t unknown00[0x10];
		std::uint32_t material_count;
		std::uint32_t unknown14;
		CollisionMaterial* materials;
	};

	static_assert(sizeof(clipMap_t) == 0x20);
	static_assert(offsetof(clipMap_t, materials) == 0x18);
}
