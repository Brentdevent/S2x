#include <std_include.hpp>

#include "texture.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>

namespace images::textures
{
	namespace
	{
		// Native format tables: MP 0xB4C970 (42 entries), SP 0x8636E0 (first 40 entries).
		constexpr std::array<std::uint32_t, 42> native_formats{0, 28, 29, 65, 61, 49, 51, 71, 72, 74, 75, 77, 78, 80,
			83, 84, 95, 96, 98, 99, 11, 35, 37, 56, 2, 10, 16, 34, 41, 54, 26, 24, 55, 40, 45, 45, 20, 85, 62, 57, 17,
			3};
	}

	std::uint32_t dxgi_format(const game::GfxImage& image)
	{
		const auto count = game::environment::is_singleplayer() ? 40 : native_formats.size();

		return image.format >= 0 && std::size_t(image.format) < count ? native_formats[image.format] : 0;
	}

	std::uint32_t stream_count(const game::GfxImage& image)
	{
		// Native RVA 0x25D2F0 stops at the first zero-width stream record.
		std::uint32_t count{};
		while (count < image.streams.size() && image.streams[count].width)
		{
			++count;
		}

		return count;
	}

	const utils::dds::mip_level* select_mip(
		const utils::dds::image& replacement, const game::GfxImage& image, const std::optional<std::uint32_t> part)
	{
		if (image.map_type != 3 || image.depth != 1 || image.num_elements > 1
			|| !replacement.matches_format(dxgi_format(image)))
		{
			return nullptr;
		}

		const auto& top = replacement.mips().front();
		const auto parts = stream_count(image);
		if (part || parts)
		{
			// Each streaming record describes a cumulative mip tail, not just the
			// newly requested mip. Validate all parts before replacing any part.
			std::uint32_t full_width{};
			std::uint32_t full_height{};
			for (auto i = 0u; i < parts; ++i)
			{
				const auto& stream = image.streams[i];
				std::size_t size{};
				if (!replacement.find_mip(stream.width, stream.height, stream.mip_count(), &size)
					|| size != stream.size())
				{
					return nullptr;
				}

				full_width = std::max(full_width, std::uint32_t(stream.width));
				full_height = std::max(full_height, std::uint32_t(stream.height));
			}

			if (top.width != full_width || top.height != full_height)
			{
				return nullptr;
			}

			if (part)
			{
				if (*part >= parts)
				{
					return nullptr;
				}

				const auto& stream = image.streams[*part];

				return replacement.find_mip(stream.width, stream.height, stream.mip_count());
			}
		}
		else if (top.width != image.width || top.height != image.height)
		{
			return nullptr;
		}

		std::size_t size{};
		const auto* mip = replacement.find_mip(image.width, image.height, image.mip_count, &size);

		return mip && size == image.data_size ? mip : nullptr;
	}

	const char* format_name(const std::uint32_t format)
	{
		switch (format)
		{
		case 28:
			return "RGBA8_UNORM";
		case 29:
			return "RGBA8_UNORM_SRGB";
		case 71:
			return "BC1_UNORM";
		case 72:
			return "BC1_UNORM_SRGB";
		case 74:
			return "BC2_UNORM";
		case 75:
			return "BC2_UNORM_SRGB";
		case 77:
			return "BC3_UNORM";
		case 78:
			return "BC3_UNORM_SRGB";
		case 80:
			return "BC4_UNORM";
		case 83:
			return "BC5_UNORM";
		case 84:
			return "BC5_SNORM";
		case 95:
			return "BC6H_UF16";
		case 96:
			return "BC6H_SF16";
		case 98:
			return "BC7_UNORM";
		case 99:
			return "BC7_UNORM_SRGB";
		default:
			return "unsupported";
		}
	}

	bool stream_placeholder(const game::GfxImage& image, const D3D11_TEXTURE2D_DESC& desc)
	{
		const auto parts = stream_count(image);
		if (!parts || image.data_size || desc.Width != 1 || desc.Height != 1 || desc.MipLevels != 1)
		{
			return false;
		}

		for (unsigned int i = 0; i < parts; ++i)
		{
			if (image.streams[i].width == 1 && image.streams[i].height == 1 && image.streams[i].mip_count() == 1)
			{
				return false;
			}
		}

		return true;
	}

	// Resource dimensions describe the current resident tail, whereas a streamed
	// GfxImage can still describe its initial placeholder. Never upload mip zero
	// from the full DDS into mip zero of a smaller streaming resource.
	const utils::dds::mip_level* reload_mip(
		const utils::dds::image& data, const game::GfxImage& image, const D3D11_TEXTURE2D_DESC& desc)
	{
		const auto parts = stream_count(image);
		if (!select_mip(data, image, parts ? std::optional<std::uint32_t>(0) : std::nullopt))
		{
			throw std::runtime_error("DDS does not match the image format, dimensions or complete mip chain");
		}

		if (parts)
		{
			bool tail = false;
			for (unsigned int i = 0; i < parts; ++i)
			{
				tail |= desc.Width == image.streams[i].width && desc.Height == image.streams[i].height
					&& desc.MipLevels == image.streams[i].mip_count();
			}

			if (!tail)
			{
				if (stream_placeholder(image, desc))
				{
					return nullptr;
				}

				throw std::runtime_error("GPU resource is not a declared streaming mip tail");
			}
		}
		else if (desc.Width != image.width || desc.Height != image.height || desc.MipLevels != image.mip_count)
		{
			throw std::runtime_error("GPU resource dimensions no longer match the resident image");
		}

		if (desc.ArraySize != 1 || desc.SampleDesc.Count != 1 || desc.Usage != D3D11_USAGE_DEFAULT
			|| desc.CPUAccessFlags || (desc.BindFlags & D3D11_BIND_DEPTH_STENCIL)
			|| (desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE)
			|| static_cast<std::uint32_t>(desc.Format) != dxgi_format(image))
		{
			throw std::runtime_error("GPU resource is not a matching ordinary writable 2D texture");
		}

		const auto* mip = data.find_mip(desc.Width, desc.Height, desc.MipLevels);
		if (!mip)
		{
			throw std::runtime_error("DDS is missing the resident mip tail");
		}

		return mip;
	}

	Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_2d(ID3D11Resource* resource)
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		if (!resource || FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture))))
		{
			throw std::runtime_error("GPU resource is not a 2D texture");
		}

		return texture;
	}

	std::string read_texture(const game::GfxImage& image)
	{
		graphics_lock graphics;
		const auto texture = texture_2d(static_cast<ID3D11Resource*>(image.texture.resource));
		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		const auto format = dxgi_format(image);
		if (desc.Width != image.width || desc.Height != image.height || desc.MipLevels != image.mip_count
			|| desc.ArraySize != 1 || desc.SampleDesc.Count != 1 || static_cast<std::uint32_t>(desc.Format) != format
			|| (desc.BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_UNORDERED_ACCESS))
			|| (desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE))
		{
			throw std::runtime_error("resident resource is not a matching ordinary 2D image");
		}

		const auto mips = utils::dds::layout(image.width, image.height, image.mip_count, format);
		std::string pixels(mips.back().offset + mips.back().size, '\0');
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		texture->GetDevice(&device);
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags = 0;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
		if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging)))
		{
			throw std::runtime_error("cannot create texture readback resource");
		}

		auto* context = immediate_context();
		context->CopyResource(staging.Get(), texture.Get());
		for (unsigned int level = 0; level < mips.size(); ++level)
		{
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(context->Map(staging.Get(), level, D3D11_MAP_READ, 0, &mapped)))
			{
				throw std::runtime_error("cannot map texture readback resource");
			}

			struct unmap
			{
				ID3D11DeviceContext* context;
				ID3D11Resource* resource;
				unsigned int level;
				~unmap()
				{
					context->Unmap(resource, level);
				}
			} release{context, staging.Get(), level};

			const auto& mip = mips[level];
			const auto rows = format == 28 || format == 29 ? mip.height : (mip.height + 3) / 4;
			const auto pitch = mip.size / rows;
			if (!mapped.pData || mapped.RowPitch < pitch)
			{
				throw std::runtime_error("invalid texture readback pitch");
			}

			for (unsigned int row = 0; row < rows; ++row)
			{
				std::memcpy(pixels.data() + mip.offset + row * pitch,
					static_cast<const char*>(mapped.pData) + std::size_t(row) * mapped.RowPitch, pitch);
			}
		}

		return pixels;
	}

	bool reload_texture(ID3D11DeviceContext* context, ID3D11Texture2D* texture, const game::GfxImage& image,
		const utils::dds::image& data)
	{
		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		const auto* mip = reload_mip(data, image, desc);
		if (!mip)
		{
			return false; // Leave native placeholders alone until streaming.
		}

		const auto format = dxgi_format(image);
		const bool rgba = format == 28 || format == 29;
		for (unsigned int level = 0; level < desc.MipLevels; ++level, ++mip)
		{
			const auto rows = rgba ? mip->height : (mip->height + 3) / 4;
			const auto pitch = static_cast<UINT>(mip->size / rows);
			context->UpdateSubresource(texture, level, nullptr, data.pixels(*mip), pitch, static_cast<UINT>(mip->size));
		}

		return true;
	}

	graphics_lock::graphics_lock()
	{
		if (!*reinterpret_cast<HANDLE*>(game::select(0xFB5838, 0xAE2D68)) || !immediate_context())
		{
			throw std::runtime_error("graphics device is not ready");
		}

		// Use the native lock shared by streaming and other immediate-context users.
		utils::hook::invoke<void>(game::select(0xEAD20, 0xD2410));
	}

	graphics_lock::~graphics_lock()
	{
		// Native release sequence, e.g. MP 0x25CDFC / SP 0xD773E.
		if (!--*reinterpret_cast<int*>(game::select(0xF9FF88, 0xADF4B8)))
		{
			*reinterpret_cast<int*>(game::select(0xF9FF8C, 0xADF4BC)) = -1;
		}

		ReleaseMutex(*reinterpret_cast<HANDLE*>(game::select(0xFB5838, 0xAE2D68)));
		InterlockedDecrement(reinterpret_cast<volatile LONG*>(game::select(0xF9FF90, 0xADF4C0)));
	}

	ID3D11DeviceContext* immediate_context()
	{
		return *reinterpret_cast<ID3D11DeviceContext**>(game::select(0xF9FF80, 0xADF4B0));
	}
}
