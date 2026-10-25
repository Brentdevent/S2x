#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace game
{
	// Shared SP/MP layout: loaders 0x616BE0 / 0x897A70 and pool indexers
	// 0x2ABF20 / 0xA24F0 verify the fields and 0x60-byte image stride.
	struct GfxImageTexture
	{
		void* resource;
		void* shader_resource_view;
		void* unordered_access_view;
	};

	static_assert(sizeof(GfxImageTexture) == 0x18);

	struct GfxImageStream
	{
		std::uint16_t width;
		std::uint16_t height;
		std::uint32_t size_and_mip_count;

		std::uint32_t size() const
		{
			return size_and_mip_count & 0x3FFFFFF;
		}

		std::uint32_t mip_count() const
		{
			return size_and_mip_count >> 26;
		}
	};

	static_assert(sizeof(GfxImageStream) == 8);
	static_assert(offsetof(GfxImageStream, size_and_mip_count) == 4);

	struct GfxImage
	{
		const char* name;
		GfxImageTexture texture;
		std::uint8_t* pixel_data;
		std::array<GfxImageStream, 4> streams;
		std::int32_t format;
		std::uint16_t width;
		std::uint16_t height;
		std::uint16_t depth;
		std::uint16_t num_elements;
		std::uint32_t data_size;
		std::uint8_t map_type;
		std::uint8_t category;
		std::uint8_t semantic;
		std::uint8_t flags;
		std::uint8_t mip_count;
		std::uint8_t unknown[3];
	};

	static_assert(sizeof(GfxImage) == 0x60);
	static_assert(alignof(GfxImage) == 8);
	static_assert(offsetof(GfxImage, name) == 0);
	static_assert(offsetof(GfxImage, texture) == 8);
	static_assert(offsetof(GfxImage, pixel_data) == 0x20);
	static_assert(offsetof(GfxImage, streams) == 0x28);
	static_assert(offsetof(GfxImage, format) == 0x48);
	static_assert(offsetof(GfxImage, width) == 0x4C);
	static_assert(offsetof(GfxImage, height) == 0x4E);
	static_assert(offsetof(GfxImage, depth) == 0x50);
	static_assert(offsetof(GfxImage, num_elements) == 0x52);
	static_assert(offsetof(GfxImage, data_size) == 0x54);
	static_assert(offsetof(GfxImage, map_type) == 0x58);
	static_assert(offsetof(GfxImage, category) == 0x59);
	static_assert(offsetof(GfxImage, semantic) == 0x5A);
	static_assert(offsetof(GfxImage, flags) == 0x5B);
	static_assert(offsetof(GfxImage, mip_count) == 0x5C);

	struct ImagePackageEntry
	{
		std::uint64_t offset_and_size;
		const char** package;
	};

	static_assert(sizeof(ImagePackageEntry) == 0x10);
	static_assert(offsetof(ImagePackageEntry, package) == 8);
}
