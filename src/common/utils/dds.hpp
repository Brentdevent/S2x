#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace utils::dds
{
	constexpr std::size_t max_file_size = 64 * 1024 * 1024;

	struct mip_level
	{
		std::uint32_t width;
		std::uint32_t height;
		std::size_t offset;
		std::size_t size;
	};

	// Validated tight mip layout and a DX10 DDS writer. Offsets start at zero.
	std::vector<mip_level> layout(
		std::uint32_t width, std::uint32_t height, std::uint32_t levels, std::uint32_t format);
	std::string encode(std::uint32_t width, std::uint32_t height, std::uint32_t levels, std::uint32_t format,
		const std::string_view pixels);

	// A single, tightly packed 2D texture. Owns the bytes used by native uploads.
	// Format values are DXGI_FORMAT numbers; legacy DDS has no sRGB tag.
	class image
	{
	public:
		explicit image(std::string bytes);

		bool matches_format(std::uint32_t dxgi_format) const;
		const mip_level* find_mip(
			std::uint32_t width, std::uint32_t height, std::uint32_t count, std::size_t* byte_count = nullptr) const;
		const std::uint8_t* pixels(const mip_level& mip) const;
		const std::vector<mip_level>& mips() const
		{
			return mips_;
		}

		std::size_t size() const
		{
			return bytes_.size();
		}

	private:
		std::string bytes_;
		std::vector<mip_level> mips_;
		std::uint32_t format_{};
		bool legacy_{};
	};
}
