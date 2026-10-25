#include <std_include.hpp>

#include "originals.hpp"
#include "texture.hpp"
#include "package.hpp"
#include "game/game.hpp"

namespace images::originals
{
	struct resident_image
	{
		std::string name;
		std::string pixels;
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint32_t mips{};
		std::uint32_t format{};
		std::uint64_t age{};
	};

	namespace
	{
		std::atomic_bool has_originals{};

		struct original_cache
		{
			std::mutex mutex;
			std::unordered_map<const void*, std::shared_ptr<const resident_image>> entries;
			std::size_t bytes{};
			std::uint64_t age{};
		};

		original_cache& get_cache()
		{
			static original_cache cache;

			return cache;
		}

		void erase_original(original_cache& cache, const void* resource)
		{
			const auto it = cache.entries.find(resource);
			if (it == cache.entries.end())
			{
				return;
			}

			if (it->second)
			{
				cache.bytes -= it->second->pixels.size();
			}

			cache.entries.erase(it);
			has_originals = !cache.entries.empty();
		}

		void store_original(const void* resource, std::shared_ptr<const resident_image> value)
		{
			auto& cache = get_cache();
			std::lock_guard lock(cache.mutex);
			erase_original(cache, resource);
			const auto size = value ? value->pixels.size() : 0;
			constexpr auto limit = std::size_t(256) * 1024 * 1024;
			while (cache.bytes > limit - size)
			{
				const auto oldest = std::min_element(cache.entries.begin(), cache.entries.end(),
					[](const auto& a, const auto& b)
					{
						return (a.second ? a.second->age : UINT64_MAX) < (b.second ? b.second->age : UINT64_MAX);
					});
				cache.bytes -= oldest->second->pixels.size();
				oldest->second.reset();
			}

			// Keep an empty entry if a customized resource has no retained original.
			// Reading its GPU pixels would otherwise export the replacement as stock.
			cache.entries.insert_or_assign(resource, std::move(value));
			cache.bytes += size;
			has_originals = true;
		}
	}

	void describe(image_source& result, const game::GfxImage& image, const bool readback)
	{
		result.width = image.width;
		result.height = image.height;
		result.mips = image.mip_count;
		result.format = textures::dxgi_format(image);
		result.native_format = image.format;
		result.map_type = image.map_type;
		const auto count = textures::stream_count(image);
		if (count)
		{
			const auto& top = image.streams[count - 1];
			result.width = top.width;
			result.height = top.height;
			result.mips = top.mip_count();
		}

		if (image.map_type != 3 || image.depth != 1 || image.num_elements > 1)
		{
			throw std::runtime_error("only ordinary 2D textures can be exported");
		}

		const auto mips = utils::dds::layout(result.width, result.height, result.mips, result.format);
		const auto total = mips.back().offset + mips.back().size;
		if (!count)
		{
			{
				auto& cache = get_cache();
				std::lock_guard lock(cache.mutex);
				const auto entry = cache.entries.find(image.texture.resource);
				if (entry != cache.entries.end())
				{
					if (!entry->second)
					{
						throw std::runtime_error("original upload unavailable for customized texture; reload the asset");
					}

					result.resident = entry->second;
				}
			}

			result.source = "retained-original";
			if (!result.resident)
			{
				if (!readback)
				{
					return;
				}

				// Unmodified resident images are read only for an explicit export/reload.
				auto value = std::make_shared<resident_image>();
				value->name = result.name;
				value->width = result.width;
				value->height = result.height;
				value->mips = result.mips;
				value->format = result.format;
				value->pixels = textures::read_texture(image);
				{
					auto& cache = get_cache();
					std::lock_guard lock(cache.mutex);
					value->age = ++cache.age;
				}

				result.resident = std::move(value);
				result.source = "resident-gpu";
			}

			const auto& data = *result.resident;
			if (data.name != result.name || data.width != result.width || data.height != result.height
				|| data.mips != result.mips || data.format != result.format || data.pixels.size() != total)
			{
				throw std::runtime_error("original upload metadata no longer matches");
			}

			return;
		}

		result.source = "original-pak";

		// Both binaries use a 0x60-byte pool stride and four PAK entries per image.
		const auto pool = game::select(0x476C138, 0x5C9FE98);
		const auto address = reinterpret_cast<std::uintptr_t>(&image);
		if (address < pool || (address - pool) % sizeof(game::GfxImage)
			|| (address - pool) / sizeof(game::GfxImage) >= 0x9D00)
		{
			throw std::runtime_error("image is outside the native image pool");
		}

		const auto index = (address - pool) / sizeof(game::GfxImage);
		const auto* entries =
			reinterpret_cast<const game::ImagePackageEntry*>(game::select(0x18A4680, 0x6BE1600)) + index * 4;
		for (auto i = 0u; i < count; ++i)
		{
			const auto& stream = image.streams[i];
			const auto mip = std::find_if(mips.begin(), mips.end(),
				[&](const auto& level)
				{
					return level.width == stream.width && level.height == stream.height;
				});
			if (mip == mips.end() || stream.size() != total - mip->offset
				|| stream.mip_count() != std::size_t(mips.end() - mip)
				|| (i
					&& (stream.size() <= image.streams[i - 1].size()
						|| stream.mip_count() != image.streams[i - 1].mip_count() + 1)))
			{
				throw std::runtime_error("inconsistent streamed mip layout");
			}

			const auto& entry = entries[i];
			if (!entry.package || !*entry.package)
			{
				throw std::runtime_error("missing image package descriptor");
			}

			// The package name follows the nine-byte file key; copy it while the asset is owned.
			const auto* name = *entry.package + 9;
			const auto length = strnlen(name, 240);
			const std::string package(name, length);
			if (!length || length == 240 || package.find_first_of("/\\:") != std::string::npos || package == "."
				|| package == "..")
			{
				throw std::runtime_error("invalid image package filename");
			}

			const auto size = static_cast<std::uint32_t>(entry.offset_and_size >> 36);
			const auto offset = entry.offset_and_size & 0xFFFFFFFFF;
			if (size < 12 || size > utils::dds::max_file_size || offset % 4)
			{
				throw std::runtime_error("invalid image package bounds");
			}

			result.parts.push_back({package, offset, size, stream.size() - (i ? image.streams[i - 1].size() : 0)});
		}
	}

	std::string read_dds(const std::filesystem::path& directory, const image_source& image)
	{
		std::string pixels;
		if (image.resident)
		{
			pixels = image.resident->pixels;
		}
		else
		{
			for (auto part = image.parts.rbegin(); part != image.parts.rend(); ++part)
			{
				std::ifstream file(directory / part->name, std::ios::binary | std::ios::ate);
				const auto length = file.tellg();
				if (!file || length < 0 || part->offset > static_cast<std::uint64_t>(length)
					|| part->size > static_cast<std::uint64_t>(length) - part->offset)
				{
					throw std::runtime_error("cannot read package range: " + part->name);
				}

				std::string bytes(part->size, '\0');
				file.seekg(static_cast<std::streamoff>(part->offset));
				if (!file.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
				{
					throw std::runtime_error("incomplete package read: " + part->name);
				}

				pixels += package::decode(bytes, part->decoded_size);
			}
		}

		return utils::dds::encode(image.width, image.height, image.mips, image.format, pixels);
	}

	struct original_source
	{
		image_source image;
		std::filesystem::path directory;
	};

	original_ptr snapshot_original(const game::GfxImage& image, const bool readback)
	{
		auto result = std::make_shared<original_source>();
		result->image.name = image.name ? image.name : "";
		describe(result->image, image, readback);
		if (!textures::stream_count(image) && !result->image.resident)
		{
			throw std::runtime_error("original resident pixels are unavailable");
		}

		result->directory = std::filesystem::current_path();

		return result;
	}

	bool same_original(const original_ptr& a, const original_ptr& b)
	{
		if (!a || !b)
		{
			return false;
		}

		const auto& x = a->image;
		const auto& y = b->image;

		return a == b
			|| (x.name == y.name && x.width == y.width && x.height == y.height && x.mips == y.mips
				&& x.format == y.format && x.resident == y.resident && x.parts == y.parts
				&& a->directory == b->directory);
	}

	std::shared_ptr<const utils::dds::image> read_original(const original_ptr& source)
	{
		if (!source)
		{
			throw std::runtime_error("original texture source is unavailable");
		}

		return std::make_shared<const utils::dds::image>(read_dds(source->directory, source->image));
	}

	void retain_original(const void* resource, const original_ptr& source)
	{
		if (source && source->image.resident)
		{
			store_original(resource, source->image.resident);
		}
	}

	void forget_original(const game::GfxImageTexture* texture) noexcept
	{
		if (!texture || !has_originals.load())
		{
			return;
		}

		try
		{
			auto& cache = get_cache();
			std::lock_guard lock(cache.mutex);
			erase_original(cache, texture->resource);
		}
		catch (...)
		{
		}
	}

	void capture_original(const game::GfxImage* image, const std::uint8_t* pixels) noexcept
	{
		if (!image || !image->texture.resource)
		{
			return;
		}

		try
		{
			store_original(image->texture.resource, {});
			if (!pixels || !image->name || textures::stream_count(*image)
				|| image->map_type != 3 || image->depth != 1 || image->num_elements > 1)
			{
				return;
			}

			const auto format = textures::dxgi_format(*image);
			const auto mips = utils::dds::layout(image->width, image->height, image->mip_count, format);
			const auto size = mips.back().offset + mips.back().size;
			if (size != image->data_size)
			{
				return;
			}

			auto value = std::make_shared<resident_image>();
			value->name = image->name;
			value->width = image->width;
			value->height = image->height;
			value->mips = image->mip_count;
			value->format = format;
			value->pixels.assign(reinterpret_cast<const char*>(pixels), size);

			auto& cache = get_cache();
			{
				std::lock_guard lock(cache.mutex);
				value->age = ++cache.age;
			}

			// DB loading can copy the GfxImage record; the resource identity survives.
			store_original(image->texture.resource, std::move(value));
		}
		catch (...)
		{
			// Failure to retain an original must not interrupt the native upload.
		}
	}
}
