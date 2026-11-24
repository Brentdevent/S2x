#pragma once

#include <cstddef>
#include <cstdint>

namespace game
{
	struct XModel;

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

	// MP 0x4E0160 indexes poses by 0x3C and loads the origin from +0x10.
	struct DynEntityPose
	{
		float quat[4];
		float origin[3];
		char __pad0[0x20];
	};
	static_assert(sizeof(DynEntityPose) == 0x3C);
	static_assert(offsetof(DynEntityPose, origin) == 0x10);

	// MP 0x4DDC10 indexes clients by 0x30 and stores the active model at +0x18.
	struct DynEntityClient
	{
		char __pad0[0x18];
		XModel* activeModel;
		char __pad1[0x10];
	};
	static_assert(sizeof(DynEntityClient) == 0x30);
	static_assert(offsetof(DynEntityClient, activeModel) == 0x18);

	// Partial collision-world layout through the dynent client lists.
	struct clipMap_t
	{
		const char* name;
		std::uint8_t unknown08[0x18];
		std::uint32_t material_count;
		std::uint32_t unknown24;
		CollisionMaterial* materials;
		std::uint8_t unknown30[0x308];
		unsigned short dynEntCount[2];
		std::uint8_t unknown33C[0x4];
		std::uint8_t unknown340[0x10]; // dynEntDefList[2], 0x78 bytes per def
		DynEntityPose* dynEntPoseList[2];
		DynEntityClient* dynEntClientList[2];
	};

	static_assert(offsetof(clipMap_t, material_count) == 0x20);
	static_assert(offsetof(clipMap_t, materials) == 0x28);
	static_assert(offsetof(clipMap_t, dynEntCount) == 0x338);
	static_assert(offsetof(clipMap_t, dynEntPoseList) == 0x350);
	static_assert(offsetof(clipMap_t, dynEntClientList) == 0x360);
}
