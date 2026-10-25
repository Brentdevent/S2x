#pragma once

#include "game/types/images.hpp"
#include <utils/dds.hpp>
#include <optional>
#include <d3d11.h>
#include <wrl/client.h>

namespace images::textures
{
	std::uint32_t dxgi_format(const game::GfxImage& image);
	std::uint32_t stream_count(const game::GfxImage& image);
	const char* format_name(std::uint32_t format);
	const utils::dds::mip_level* select_mip(
		const utils::dds::image& replacement, const game::GfxImage& image, std::optional<std::uint32_t> part = {});

	bool stream_placeholder(const game::GfxImage& image, const D3D11_TEXTURE2D_DESC& desc);
	const utils::dds::mip_level* reload_mip(
		const utils::dds::image& data, const game::GfxImage& image, const D3D11_TEXTURE2D_DESC& desc);
	Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_2d(ID3D11Resource* resource);
	std::string read_texture(const game::GfxImage& image);
	bool reload_texture(ID3D11DeviceContext* context, ID3D11Texture2D* texture, const game::GfxImage& image,
		const utils::dds::image& data);
	ID3D11DeviceContext* immediate_context();

	class graphics_lock
	{
	public:
		graphics_lock();
		~graphics_lock();
		graphics_lock(const graphics_lock&) = delete;
		graphics_lock& operator=(const graphics_lock&) = delete;
	};
}
