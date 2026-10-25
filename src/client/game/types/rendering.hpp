#pragma once

#include "materials.hpp"

#include <cstddef>
#include <cstdint>

namespace game
{
	struct GfxPositionVertex
	{
		float xyz[3];
	};

	struct DObjAnimMat
	{
		float quaternion[4];
		float translation[3];
		float weight;
	};

	static_assert(sizeof(DObjAnimMat) == 32);
	static_assert(offsetof(DObjAnimMat, translation) == 16);
	static_assert(offsetof(DObjAnimMat, weight) == 28);

	struct XSurfaceRigidList
	{
		std::uint16_t bone_offset; // Byte offset into 64-byte skin matrices.
		std::uint16_t vertex_count;
		std::uint16_t first_triangle;
		std::uint16_t triangle_count;
		const void* collision_tree;
	};

	static_assert(sizeof(XSurfaceRigidList) == 16);
	static_assert(offsetof(XSurfaceRigidList, first_triangle) == 4);
	static_assert(offsetof(XSurfaceRigidList, triangle_count) == 6);

	struct XSurfaceVertex
	{
		float xyz[3];
		std::uint32_t normal;
	};

	static_assert(sizeof(GfxPositionVertex) == 12);
	static_assert(sizeof(XSurfaceVertex) == 16);

	struct GfxSurface
	{
		std::uint32_t vertex_layer;
		std::uint32_t first_vertex;
		std::uint16_t vertex_count;
		std::uint16_t triangle_count;
		std::uint32_t unknown0C;
		const void* tessellation;
		std::uint32_t first_index;
		std::uint32_t unknown1C;
		const Material* material;
		std::uint8_t unknown28[8];
		std::uint8_t zone;
		std::uint8_t unknown31[7];
	};

	static_assert(sizeof(GfxSurface) == 56);
	static_assert(offsetof(GfxSurface, first_vertex) == 4);
	static_assert(offsetof(GfxSurface, triangle_count) == 10);
	static_assert(offsetof(GfxSurface, first_index) == 24);
	static_assert(offsetof(GfxSurface, material) == 32);
	static_assert(offsetof(GfxSurface, zone) == 48);

	struct Bounds
	{
		float midpoint[3];
		float half_size[3];
	};

	struct GfxSurfaceBounds
	{
		Bounds bounds;
		std::uint8_t unknown18[40];
	};

	struct GfxStaticModelBounds
	{
		Bounds bounds;
		float lighting_origin[3];
	};

	static_assert(sizeof(Bounds) == 24);
	static_assert(sizeof(GfxSurfaceBounds) == 64);
	static_assert(sizeof(GfxStaticModelBounds) == 36);

	// Native CPU skinners 0x8B3840 / 0x8B3DD0 read sixteen bytes per vertex.
	struct XSurfaceVertexWeights
	{
		std::uint16_t bones[4];	  // Model-local indices, not byte offsets.
		std::uint16_t weights[3]; // Influences 1..3; influence 0 is the remainder.
		std::uint16_t count;
	};

	static_assert(sizeof(XSurfaceVertexWeights) == 16);
	static_assert(offsetof(XSurfaceVertexWeights, weights) == 8);
	static_assert(offsetof(XSurfaceVertexWeights, count) == 14);

	// Partial layout through the CPU skinning weights.
	struct XStreamSurfaceInfo
	{
		const std::uint16_t* indices;
		const GfxPositionVertex* positions;
		std::uint8_t unknown10[176];
		const XSurfaceVertexWeights* weights;
	};

	static_assert(offsetof(XStreamSurfaceInfo, positions) == 8);
	static_assert(offsetof(XStreamSurfaceInfo, weights) == 192);

	// Partial layout through the loaded surface data.
	struct XStreamSurface
	{
		std::uint8_t unknown00[42];
		std::uint16_t blend_shape_count;
		std::uint8_t unknown2C[20];
		const XStreamSurfaceInfo* loaded;
	};

	static_assert(offsetof(XStreamSurface, blend_shape_count) == 42);
	static_assert(offsetof(XStreamSurface, loaded) == 64);

	struct XSurface
	{
		std::uint16_t flags;
		std::uint16_t vertex_count;
		std::uint16_t triangle_count;
		std::uint8_t rigid_count;
		std::uint8_t unknown07[9];
		const XStreamSurface* stream;
		const std::uint16_t* indices;
		const XSurfaceRigidList* rigid_lists;
		const XSurfaceVertex* vertices;
		std::uint8_t unknown30[56];
	};

	static_assert(sizeof(XSurface) == 104);
	static_assert(offsetof(XSurface, vertex_count) == 2);
	static_assert(offsetof(XSurface, triangle_count) == 4);
	static_assert(offsetof(XSurface, indices) == 24);
	static_assert(offsetof(XSurface, vertices) == 40);

	struct XModelLodInfo
	{
		float distance;
		std::uint16_t surface_count;
		std::uint16_t first_surface;
		std::uint8_t unknown08[64];
		const XSurface* surfaces;
	};

	static_assert(sizeof(XModelLodInfo) == 80);
	static_assert(offsetof(XModelLodInfo, surfaces) == 72);

	// Partial layout through the LOD count.
	struct XModel
	{
		std::uint8_t unknown00[8];
		std::uint16_t bone_count;
		std::uint8_t unknown0A[118];
		const DObjAnimMat* base_pose;
		XModelLodInfo lods[6];
		std::uint8_t lod_count;
	};

	static_assert(offsetof(XModel, base_pose) == 128);
	static_assert(offsetof(XModel, lods) == 136);
	static_assert(offsetof(XModel, lod_count) == 616);

	// Partial layout through the composite part count.
	struct XModelBase
	{
		const char* name;
		const XModel* model;
		const Material* const* materials;
		const void* unknown18;
		std::uint32_t flags;
		std::uint8_t unknown24[6];
		std::uint8_t part_count;
	};

	static_assert(offsetof(XModelBase, part_count) == 42);
	static_assert(offsetof(XModelBase, model) == 8);
	static_assert(offsetof(XModelBase, materials) == 16);
	static_assert(offsetof(XModelBase, flags) == 32);

	struct XModelCompositePart
	{
		const XModelBase* base;
		std::uint8_t unknown08[40];
	};

	static_assert(sizeof(XModelCompositePart) == 48);

	// 0x873E60 receives this surface context from 0x874060. Bone offsets and
	// the base pose are already resolved for the selected model/composite LOD.
	struct SkinSurfaceContext
	{
		std::uint8_t unknown00[48];
		const DObjAnimMat* base_pose;
		std::uint16_t bone_offset;
		std::uint16_t bone_count; // Highest bone used by this LOD, plus one.
		std::uint8_t unknown3C[12];
	};

	static_assert(sizeof(SkinSurfaceContext) == 72);
	static_assert(offsetof(SkinSurfaceContext, base_pose) == 48);
	static_assert(offsetof(SkinSurfaceContext, bone_offset) == 56);
	static_assert(offsetof(SkinSurfaceContext, bone_count) == 58);

	// Partial layout through the model list.
	struct DObj
	{
		std::uint8_t unknown00[16];
		std::uint8_t model_count;
		std::uint8_t unknown11;
		std::uint16_t bone_count;
		std::uint8_t unknown14[2];
		std::uint16_t extra_bone_count;
		std::uint8_t unknown18[232];
		const DObjAnimMat* bones;
		std::uint8_t unknown108[52];
		std::uint32_t hidden[13];
		const void* models; // Model entries differ between SP and MP; use the native accessor.
	};

	static_assert(offsetof(DObj, model_count) == 16);
	static_assert(offsetof(DObj, bone_count) == 18);
	static_assert(offsetof(DObj, extra_bone_count) == 22);
	static_assert(offsetof(DObj, models) == 368);
	static_assert(offsetof(DObj, bones) == 256);
	static_assert(offsetof(DObj, hidden) == 316);

	struct FxMarkTriangles
	{
		const FxMarkTriangles* next;
		std::uint64_t context;
		std::uint16_t count;
		std::uint16_t indices[1]; // Variable-length count * 3 indices.
	};

	static_assert(offsetof(FxMarkTriangles, count) == 16);
	static_assert(offsetof(FxMarkTriangles, indices) == 18);

	// Partial layout through the fade flags.
	struct FxMark
	{
		const Material* material;
		const FxMarkTriangles* triangles;
		const void* points;
		const void* cached_vertices;
		std::uint64_t context;
		std::uint16_t triangle_count;
		std::uint16_t vertex_count;
		std::uint8_t unknown2C[16];
		std::int32_t created_time;
		std::uint8_t unknown40[64];
		float alpha;
		std::uint8_t unknown84[52];
		std::uint8_t fade_in;
		std::uint8_t fade_out;
		std::uint8_t flags;
	};

	static_assert(offsetof(FxMark, created_time) == 60);
	static_assert(offsetof(FxMark, alpha) == 128);
	static_assert(offsetof(FxMark, fade_in) == 184);
	static_assert(offsetof(FxMark, triangles) == 8);
	static_assert(offsetof(FxMark, triangle_count) == 40);
	static_assert(offsetof(FxMark, vertex_count) == 42);

	struct FxMarkVertex
	{
		float xyz[3];
		std::uint8_t unknown0C[32];
	};

	static_assert(sizeof(FxMarkVertex) == 44);

	struct GfxStaticModelDrawInst
	{
		float origin[3];
		float axis[3][3];
		float scale[3];
		std::uint8_t unknown3C[12];
		const XModelBase* model;
		std::uint8_t unknown50[48];
	};

	static_assert(sizeof(GfxStaticModelDrawInst) == 128);
	static_assert(offsetof(GfxStaticModelDrawInst, axis) == 12);
	static_assert(offsetof(GfxStaticModelDrawInst, scale) == 48);
	static_assert(offsetof(GfxStaticModelDrawInst, model) == 72);

	// Partial layout through the vertex data.
	struct GfxWorldZone
	{
		const char* name;
		std::uint32_t index;
		std::uint32_t vertex_count;
		const GfxPositionVertex* vertices;
	};

	static_assert(sizeof(GfxWorldZone) == 24);
	static_assert(offsetof(GfxWorldZone, vertices) == 16);

	// Partial layout through the static model instances.
	struct GfxWorld
	{
		const char* name;
		std::uint8_t unknown08[20];
		std::uint32_t surface_count;
		std::uint8_t unknown20[0x318];
		std::uint32_t zone_count;
		std::uint32_t unknown33C;
		const GfxWorldZone* zones[16];
		std::uint8_t unknown3C0[8];
		std::uint32_t index_count;
		std::uint32_t unknown3CC;
		const std::uint16_t* indices;
		std::uint8_t unknown3D8[0x8D0];
		std::uint32_t model_count;
		std::uint8_t unknownCAC[0x2EC];
		const GfxSurface* surfaces;
		const GfxSurfaceBounds* surface_bounds;
		const void* unknownFA8;
		const GfxStaticModelBounds* model_bounds;
		const GfxStaticModelDrawInst* models;
	};

	static_assert(sizeof(GfxWorld) == 0xFC0);
	static_assert(offsetof(GfxWorld, surface_count) == 0x1C);
	static_assert(offsetof(GfxWorld, zone_count) == 0x338);
	static_assert(offsetof(GfxWorld, zones) == 0x340);
	static_assert(offsetof(GfxWorld, index_count) == 0x3C8);
	static_assert(offsetof(GfxWorld, indices) == 0x3D0);
	static_assert(offsetof(GfxWorld, model_count) == 0xCA8);
	static_assert(offsetof(GfxWorld, surfaces) == 0xF98);
	static_assert(offsetof(GfxWorld, surface_bounds) == 0xFA0);
	static_assert(offsetof(GfxWorld, model_bounds) == 0xFB0);
	static_assert(offsetof(GfxWorld, models) == 0xFB8);
}
