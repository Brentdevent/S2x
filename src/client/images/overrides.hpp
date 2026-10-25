#pragma once

#include "game/types/images.hpp"
#include <utils/dds.hpp>
#include <memory>
#include <optional>

namespace images::overrides
{
	struct pixel_override
	{
		std::shared_ptr<const utils::dds::image> owner;
		const std::uint8_t* pixels{};
	};

	pixel_override find(const game::GfxImage* image, std::optional<std::uint32_t> part = {}) noexcept;
	void track_upload(const game::GfxImageTexture* texture, const game::GfxImage* image,
		const pixel_override& replacement, const std::uint8_t* original_pixels = nullptr) noexcept;
	void track_copy(const game::GfxImageTexture* destination, const game::GfxImageTexture* source) noexcept;
	void release(const game::GfxImageTexture* texture);
	void initialize();
	void shutdown();
}
