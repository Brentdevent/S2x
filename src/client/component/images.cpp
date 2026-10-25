#include <std_include.hpp>

#include "loader/component_loader.hpp"
#include "game/game.hpp"
#include "filesystem.hpp"
#include "images/overrides.hpp"
#include "images/originals.hpp"
#include "images/export.hpp"
#include <utils/hook.hpp>

namespace images
{
	namespace
	{
		utils::hook::detour load_texture_hook;
		utils::hook::detour upload_stream_hook;
		utils::hook::detour release_texture_hook;
		utils::hook::detour release_image_hook;
		utils::hook::detour release_image_reference_hook;
		utils::hook::detour copy_texture_hook;

		void load_texture_stub(const std::uint8_t** pixels, game::GfxImage* image)
		{
			const auto replacement = overrides::find(image);
			auto* replacement_pixels = replacement.pixels;

			// Native creation consumes the pixels synchronously without retaining the pointer.
			load_texture_hook.invoke<void>(replacement_pixels ? &replacement_pixels : pixels, image);

			overrides::track_upload(&image->texture, image, replacement, pixels ? *pixels : nullptr);
		}

		void upload_stream_stub(
			game::GfxImageTexture* texture, game::GfxImage* image, const std::uint32_t part, const std::uint8_t* pixels)
		{
			const auto replacement = overrides::find(image, part);
			upload_stream_hook.invoke<void>(texture, image, part, replacement.pixels ? replacement.pixels : pixels);
			overrides::track_upload(texture, image, replacement);
		}

		void forget_texture(const game::GfxImageTexture* texture)
		{
			overrides::release(texture);
			originals::forget_original(texture);
		}

		std::uint64_t release_texture_stub(game::GfxImageTexture* texture)
		{
			forget_texture(texture);

			return release_texture_hook.invoke<std::uint64_t>(texture);
		}

		std::uint64_t release_image_stub(game::GfxImage* image)
		{
			forget_texture(&image->texture);

			return release_image_hook.invoke<std::uint64_t>(image);
		}

		void release_image_reference_stub(game::GfxImage** image)
		{
			if (*image)
			{
				forget_texture(&(*image)->texture);
			}

			release_image_reference_hook.invoke<void>(image);
		}

		std::uint64_t copy_texture_stub(game::GfxImageTexture* destination, const unsigned int destination_mip,
			const game::GfxImageTexture* source, const unsigned int source_mip, const int count)
		{
			const auto result =
				copy_texture_hook.invoke<std::uint64_t>(destination, destination_mip, source, source_mip, count);
			overrides::track_copy(destination, source);

			return result;
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated())
			{
				return;
			}

			// Initialize custom search paths before DB/render threads request files.
			filesystem::get_search_paths();

			load_texture_hook.create(game::select(0x897A70, 0x616BE0), load_texture_stub);
			upload_stream_hook.create(game::select(0x8973A0, 0x616510), upload_stream_stub);

			// These paths release resources independently. Retire tracking before native
			// teardown so imageReload cannot visit an unloaded texture.
			release_texture_hook.create(game::select(0x897300, 0x616470), release_texture_stub);
			release_image_hook.create(game::select(0x897210, 0x616380), release_image_stub);
			release_image_reference_hook.create(game::select(0x897060, 0x6161F0), release_image_reference_stub);

			copy_texture_hook.create(game::select(0x896580, 0x615700), copy_texture_stub);
			overrides::initialize();
			exports::register_commands();
		}

		void pre_destroy() override
		{
			overrides::shutdown();
		}
	};
}

REGISTER_COMPONENT(images::component)
