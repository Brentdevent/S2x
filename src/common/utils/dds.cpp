#include "dds.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace utils::dds
{
	namespace
	{
		constexpr std::uint32_t fourcc(const char a, const char b, const char c, const char d)
		{
			return std::uint32_t(a) | (std::uint32_t(b) << 8) | (std::uint32_t(c) << 16) | (std::uint32_t(d) << 24);
		}

		std::uint32_t linear_format(const std::uint32_t format)
		{
			switch (format)
			{
			case 29:
				return 28; // RGBA8
			case 72:
				return 71; // BC1
			case 75:
				return 74; // BC2
			case 78:
				return 77; // BC3
			case 99:
				return 98; // BC7
			default:
				return format;
			}
		}

		std::size_t block_size(const std::uint32_t format)
		{
			switch (linear_format(format))
			{
			case 71:
			case 80:
			case 81:
				return 8; // BC1, BC4
			case 74:
			case 77:
			case 83:
			case 84:
			case 95:
			case 96:
			case 98:
				return 16;
			case 28:
				return 0; // tightly packed RGBA8
			default:
				throw std::runtime_error("unsupported DDS format (use RGBA8 or BC1-BC7)");
			}
		}
	}

	std::vector<mip_level> layout(
		std::uint32_t width, std::uint32_t height, const std::uint32_t levels, const std::uint32_t format)
	{
		if (!width || !height || width > 16384 || height > 16384 || !levels)
		{
			throw std::runtime_error("invalid DDS dimensions or mip count");
		}

		auto max_levels = 1u;
		for (auto size = std::max(width, height); size > 1; size >>= 1)
		{
			++max_levels;
		}

		if (levels > max_levels)
		{
			throw std::runtime_error("invalid DDS mip count");
		}

		const auto block = block_size(format);
		std::vector<mip_level> result;
		std::size_t offset{};
		for (auto i = 0u; i < levels; ++i)
		{
			const auto size =
				block ? std::size_t((width + 3) / 4) * ((height + 3) / 4) * block : std::size_t(width) * height * 4;
			if (size > max_file_size - 148 - offset)
			{
				throw std::runtime_error("DDS exceeds 64 MiB");
			}

			result.push_back({width, height, offset, size});
			offset += size;
			width = std::max(1u, width / 2);
			height = std::max(1u, height / 2);
		}

		return result;
	}

	std::string encode(const std::uint32_t width, const std::uint32_t height, const std::uint32_t levels,
		const std::uint32_t format, const std::string_view pixels)
	{
		const auto mips = layout(width, height, levels, format);
		if (pixels.size() != mips.back().offset + mips.back().size)
		{
			throw std::runtime_error("pixel payload does not match DDS mip chain");
		}

		std::string bytes(148, '\0');
		const auto put = [&](const std::size_t offset, const std::uint32_t value)
		{
			std::memcpy(bytes.data() + offset, &value, sizeof(value));
		};

		const auto compressed = block_size(format) != 0;
		put(0, fourcc('D', 'D', 'S', ' '));
		put(4, 124);
		put(8, 0x1007 | (compressed ? 0x80000 : 8) | (levels > 1 ? 0x20000 : 0));
		put(12, height);
		put(16, width);
		put(20, compressed ? static_cast<std::uint32_t>(mips.front().size) : width * 4);
		put(28, levels);
		put(76, 32);
		put(80, 4);
		put(84, fourcc('D', 'X', '1', '0'));
		put(108, 0x1000 | (levels > 1 ? 0x400008 : 0));
		put(128, format);
		put(132, 3); // D3D10_RESOURCE_DIMENSION_TEXTURE2D
		put(140, 1);
		bytes.append(pixels);

		return bytes;
	}

	image::image(std::string bytes) : bytes_(std::move(bytes))
	{
		if (bytes_.size() < 128 || bytes_.size() > max_file_size)
		{
			throw std::runtime_error("DDS file must be between 128 bytes and 64 MiB");
		}

		const auto read = [&](const std::size_t offset)
		{
			if (offset > bytes_.size() - sizeof(std::uint32_t))
			{
				throw std::runtime_error("truncated DDS header");
			}

			std::uint32_t value;
			std::memcpy(&value, bytes_.data() + offset, sizeof(value));

			return value;
		};

		if (read(0) != fourcc('D', 'D', 'S', ' ') || read(4) != 124 || read(76) != 32)
		{
			throw std::runtime_error("invalid DDS header");
		}

		auto width = read(16);
		auto height = read(12);
		const auto levels = std::max(1u, read(28));
		if (!width || !height || width > 16384 || height > 16384)
		{
			throw std::runtime_error("invalid DDS dimensions");
		}

		if (read(24) > 1 || (read(112) & 0x20FE00) || (read(8) & 0x800000))
		{
			throw std::runtime_error("only ordinary 2D DDS textures are supported");
		}

		auto max_levels = 1u;
		for (auto dimension = std::max(width, height); dimension > 1; dimension >>= 1)
		{
			++max_levels;
		}

		if (levels > max_levels)
		{
			throw std::runtime_error("invalid DDS mip count");
		}

		std::size_t offset = 128;
		legacy_ = true;
		if (read(80) & 4) // DDPF_FOURCC
		{
			switch (read(84))
			{
			case fourcc('D', 'X', 'T', '1'):
				format_ = 71;
				break;
			case fourcc('D', 'X', 'T', '3'):
				format_ = 74;
				break;
			case fourcc('D', 'X', 'T', '5'):
				format_ = 77;
				break;
			case fourcc('A', 'T', 'I', '1'):
			case fourcc('B', 'C', '4', 'U'):
				format_ = 80;
				break;
			case fourcc('B', 'C', '4', 'S'):
				format_ = 81;
				break;
			case fourcc('A', 'T', 'I', '2'):
			case fourcc('B', 'C', '5', 'U'):
				format_ = 83;
				break;
			case fourcc('B', 'C', '5', 'S'):
				format_ = 84;
				break;
			case fourcc('D', 'X', '1', '0'):
				format_ = read(128);
				if (read(132) != 3 || read(140) != 1 || read(136) != 0)
				{
					throw std::runtime_error("DDS arrays, cubemaps and volumes are unsupported");
				}

				// Unknown, straight or opaque alpha are usable without transforming pixels.
				if (read(144) != 0 && read(144) != 1 && read(144) != 3)
				{
					throw std::runtime_error("premultiplied/custom DDS alpha is unsupported");
				}

				legacy_ = false;
				offset = 148;
				break;
			default:
				throw std::runtime_error("unsupported DDS FourCC");
			}
		}
		else if ((read(80) & 0x41) == 0x41 && read(88) == 32 && read(92) == 0xFF && read(96) == 0xFF00
			&& read(100) == 0xFF0000 && read(104) == 0xFF000000)
		{
			format_ = 28;
		}
		else
		{
			throw std::runtime_error("unsupported DDS channel masks (use RGBA8 or DX10)");
		}

		const auto block = block_size(format_);
		if (!block && (read(8) & 8) && read(20) != width * 4)
		{
			throw std::runtime_error("padded DDS rows are unsupported");
		}

		for (auto i = 0u; i < levels; ++i)
		{
			const auto size =
				block ? std::size_t((width + 3) / 4) * ((height + 3) / 4) * block : std::size_t(width) * height * 4;
			if (offset > bytes_.size() || size > bytes_.size() - offset)
			{
				throw std::runtime_error("truncated DDS mip data");
			}

			mips_.push_back({width, height, offset, size});
			offset += size;
			width = std::max(1u, width / 2);
			height = std::max(1u, height / 2);
		}
	}

	bool image::matches_format(const std::uint32_t dxgi_format) const
	{
		return legacy_ ? linear_format(dxgi_format) == format_ : dxgi_format == format_;
	}

	const mip_level* image::find_mip(
		const std::uint32_t width, const std::uint32_t height, const std::uint32_t count, std::size_t* byte_count) const
	{
		if (!count)
		{
			return nullptr;
		}

		for (std::size_t i = 0; i < mips_.size(); ++i)
		{
			if (mips_[i].width != width || mips_[i].height != height || count > mips_.size() - i)
			{
				continue;
			}

			const auto& last = mips_[i + count - 1];
			if (byte_count)
			{
				*byte_count = last.offset + last.size - mips_[i].offset;
			}

			return &mips_[i];
		}

		return nullptr;
	}

	const std::uint8_t* image::pixels(const mip_level& mip) const
	{
		return reinterpret_cast<const std::uint8_t*>(bytes_.data() + mip.offset);
	}
}
