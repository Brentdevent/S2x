#pragma once

#include "game/types/images.hpp"
#include <utils/dds.hpp>
#include <filesystem>
#include <memory>

namespace images::originals
{
	struct resident_image;

	struct package_part
	{
		std::string name;
		std::uint64_t offset{};
		std::uint32_t size{};
		std::uint32_t decoded_size{};
		bool operator==(const package_part&) const = default;
	};

	struct image_source
	{
		std::string name;
		std::string source;
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint32_t mips{};
		std::uint32_t format{};
		std::uint32_t map_type{};
		std::int32_t native_format{};
		std::vector<package_part> parts;
		std::shared_ptr<const resident_image> resident;
	};

	struct original_source;
	using original_ptr = std::shared_ptr<const original_source>;

	// Stream descriptors must be copied while native asset ownership is held.
	// Resident readback requires the renderer; native upload callbacks use retained bytes.
	void describe(image_source& result, const game::GfxImage& image, bool readback = true);
	std::string read_dds(const std::filesystem::path& directory, const image_source& image);
	original_ptr snapshot_original(const game::GfxImage& image, bool readback = false);
	bool same_original(const original_ptr& a, const original_ptr& b);
	std::shared_ptr<const utils::dds::image> read_original(const original_ptr& source);
	void retain_original(const void* resource, const original_ptr& source);
	void capture_original(const game::GfxImage* image, const std::uint8_t* pixels) noexcept;
	void forget_original(const game::GfxImageTexture* texture) noexcept;
}
