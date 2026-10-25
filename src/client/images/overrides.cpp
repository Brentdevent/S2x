#include <std_include.hpp>

#include "overrides.hpp"
#include "files.hpp"
#include "originals.hpp"
#include "texture.hpp"
#include "component/command.hpp"
#include "component/console/console.hpp"
#include "component/scheduler.hpp"
#include <utils/string.hpp>

namespace images::overrides
{
	namespace
	{
		constexpr std::size_t cache_limit = 256 * 1024 * 1024;
		std::atomic_bool uploads_pending{};
		std::atomic_bool reload_busy{};
		std::atomic_bool stopping{};

		game::GfxImage describe(const game::GfxImage& image)
		{
			auto result = image;
			result.name = nullptr;
			result.pixel_data = nullptr;
			result.texture = {};

			return result;
		}

		struct cached_image
		{
			std::shared_ptr<const utils::dds::image> data;
			bool warned{};
			std::uint32_t reported_uploads{};
			game::GfxImage description{};
		};

		struct live_texture
		{
			std::string name;
			game::GfxImage description{};
			bool dirty{};
			std::uint64_t serial{};
			originals::original_ptr original;
			std::shared_ptr<const utils::dds::image> restored;
		};

		struct image_cache
		{
			std::mutex mutex;
			std::unordered_map<std::string, cached_image> images;

			// Non-owning keys, removed before native COM release. The mutex also
			// protects resource access; no GfxImage pointer is retained.
			std::unordered_map<ID3D11Resource*, live_texture> textures;
			std::size_t bytes{};
			std::uint64_t serial{};
		};

		image_cache& get_cache()
		{
			static image_cache cache;

			return cache;
		}

		bool safe_asset_name(const std::string_view name)
		{
			if (name.empty() || name.size() > 240 || name.front() == '/' || name.front() == '\\'
				|| name.find_first_of(":\\") != std::string_view::npos)
			{
				return false;
			}

			const std::filesystem::path path{name};
			for (const auto& segment : path)
			{
				if (segment == "." || segment == "..")
				{
					return false;
				}
			}

			return true;
		}

		std::shared_ptr<const utils::dds::image> read_override(const std::string& name)
		{
			if (!safe_asset_name(name))
			{
				return {};
			}

			const auto path = files::find(name);
			if (!path)
			{
				return {};
			}

			std::ifstream file(*path, std::ios::binary | std::ios::ate);
			const auto length = file.tellg();
			if (!file || length < 128 || length > std::streamoff(utils::dds::max_file_size))
			{
				throw std::runtime_error("cannot read DDS, or file exceeds 64 MiB");
			}

			std::string bytes(std::size_t(length), '\0');
			file.seekg(0);
			if (!file.read(bytes.data(), std::streamsize(bytes.size())))
			{
				throw std::runtime_error("incomplete DDS read");
			}

			return std::make_shared<const utils::dds::image>(std::move(bytes));
		}

		pixel_override find_override(const game::GfxImage* image, const std::optional<std::uint32_t> part)
		{
			if (!image || !image->name || image->map_type != 3)
			{
				return {};
			}

			// Streamed assets can initially have a 1x1 placeholder and no payload.
			if (!part && !image->data_size && textures::stream_count(*image))
			{
				return {};
			}

			auto& cache = get_cache();
			std::lock_guard lock(cache.mutex);
			if (!cache.images.contains(image->name) && !files::find(image->name))
			{
				return {};
			}

			auto [entry, inserted] = cache.images.try_emplace(image->name);
			auto& value = entry->second;
			if (inserted)
			{
				value.description = describe(*image);
				try
				{
					const auto data = read_override(entry->first);
					if (data && data->size() > cache_limit - cache.bytes)
					{
						throw std::runtime_error("custom image cache exceeds 256 MiB");
					}

					value.data = data;
					if (data)
					{
						cache.bytes += data->size();
					}
				}
				catch (const std::exception& error)
				{
					console::warn("[images] Ignoring %s.dds: %s\n", image->name, error.what());
				}
			}

			if (!value.data)
			{
				return {};
			}

			const auto* mip = textures::select_mip(*value.data, *image, part);
			if (!mip)
			{
				if (!value.warned)
				{
					value.warned = true;
					console::warn("[images] %s.dds does not match this image's type, format, dimensions or mip chain. "
								  "Use imageList to inspect it.\n",
						image->name);
				}

				return {};
			}

			const auto upload_bit = 1u << (part ? *part + 1 : 0);
			if (!(value.reported_uploads & upload_bit))
			{
				value.reported_uploads |= upload_bit;
				if (part)
				{
					console::info("[images] Using custom texture: %s (stream part %u)\n", image->name, *part);
				}
				else
				{
					console::info("[images] Using custom texture: %s (initial upload)\n", image->name);
				}
			}

			return {value.data, value.data->pixels(*mip)};
		}

		std::size_t refresh_locked(image_cache& cache)
		{
			std::size_t updated{};
			uploads_pending = false;
			for (auto& [resource, live] : cache.textures)
			{
				if (!std::exchange(live.dirty, false))
				{
					continue;
				}

				const auto found = cache.images.find(live.name);
				const auto custom = found != cache.images.end() ? found->second.data : nullptr;
				const auto data = custom ? custom : live.restored;
				if (!data)
				{
					continue;
				}

				try
				{
					const auto texture = textures::texture_2d(resource);
					if (textures::reload_texture(textures::immediate_context(), texture.Get(), live.description, *data))
					{
						++updated;
					}

					live.restored.reset();
					if (!custom && !textures::stream_count(live.description))
					{
						live.original.reset();
						game::GfxImageTexture restored{};
						restored.resource = resource;
						originals::forget_original(&restored);
					}
				}
				catch (const std::exception& error)
				{
					console::warn("[images] Cannot refresh %s: %s\n", live.name.c_str(), error.what());
				}
			}

			return updated;
		}

		void refresh_textures()
		{
			if (stopping || !uploads_pending)
			{
				return;
			}

			try
			{
				textures::graphics_lock graphics;
				auto& cache = get_cache();
				std::lock_guard lock(cache.mutex);
				refresh_locked(cache);
			}
			catch (...)
			{
				// Keep pending work for a later frame if the graphics context is unavailable.
			}
		}

		struct original_backup
		{
			std::uint64_t serial{};
			originals::original_ptr source;
			std::shared_ptr<const utils::dds::image> data;
			bool placeholder{};
		};

		struct reload_candidate
		{
			game::GfxImage description{};
			std::shared_ptr<const utils::dds::image> data;
			std::vector<original_backup> originals;
			bool had_override{};
			bool restore{};
		};

		struct reload_batch
		{
			std::map<std::string, reload_candidate> images;
			std::size_t rejected{};
			std::size_t missing{};
		};

		bool matches(const utils::dds::image& data, const game::GfxImage& image)
		{
			return textures::select_mip(
					   data, image, textures::stream_count(image) ? std::optional<std::uint32_t>(0) : std::nullopt)
				!= nullptr;
		}

		const original_backup* find_original(const reload_candidate& candidate, const live_texture& live)
		{
			for (const auto& original : candidate.originals)
			{
				if (original.source && (!candidate.restore || original.data)
					&& (original.serial == live.serial || originals::same_original(original.source, live.original)))
				{
					return &original;
				}
			}

			return nullptr;
		}

		// The caller holds both the native graphics lock and the cache mutex.
		void validate_candidate(const image_cache& cache, const std::string& name, const reload_candidate& candidate)
		{
			for (const auto& [resource, live] : cache.textures)
			{
				if (live.name != name)
				{
					continue;
				}

				const auto texture = textures::texture_2d(resource);
				D3D11_TEXTURE2D_DESC desc{};
				texture->GetDesc(&desc);
				if (textures::stream_placeholder(live.description, desc))
				{
					continue;
				}

				const auto* original = find_original(candidate, live);
				if (!original)
				{
					throw std::runtime_error(
						"original unavailable or texture changed during reload; retry imageReload");
				}

				const auto& data = candidate.restore ? original->data : candidate.data;
				if (!data)
				{
					throw std::runtime_error("original texture data is unavailable");
				}

				textures::reload_mip(*data, live.description, desc);
			}
		}

		void publish_candidate(image_cache& cache, const std::string& name, const reload_candidate& candidate)
		{
			const auto found = cache.images.find(name);
			const auto old_size = found != cache.images.end() && found->second.data ? found->second.data->size() : 0;
			const auto new_size = candidate.data ? candidate.data->size() : 0;
			if (new_size > cache_limit - (cache.bytes - old_size))
			{
				throw std::runtime_error("custom image cache exceeds 256 MiB");
			}

			auto& value = cache.images[name];
			value.data = candidate.data;
			value.description = candidate.description;
			value.warned = false;
			value.reported_uploads = 0;
			cache.bytes = cache.bytes - old_size + new_size;

			for (auto& [resource, live] : cache.textures)
			{
				if (live.name != name)
				{
					continue;
				}

				const auto* original = find_original(candidate, live);
				if (original)
				{
					originals::retain_original(resource, original->source);
					live.original = original->source;
				}

				live.restored = candidate.restore && original ? original->data : nullptr;
				live.dirty = true;
			}
		}

		void apply_reload(const std::shared_ptr<reload_batch>& batch)
		{
			struct finish
			{
				~finish()
				{
					reload_busy = false;
				}
			} done;

			if (stopping)
			{
				return;
			}

			try
			{
				textures::graphics_lock graphics;
				auto& cache = get_cache();
				std::lock_guard lock(cache.mutex);
				std::size_t refreshed{}, restored{}, rejected{batch->rejected};
				for (const auto& [name, candidate] : batch->images)
				{
					if (!candidate.data && !candidate.restore)
					{
						continue;
					}

					try
					{
						// Validate every live instance before publishing a new generation.
						// This includes pending stream resources and DB override variants.
						validate_candidate(cache, name, candidate);
						publish_candidate(cache, name, candidate);
						if (candidate.restore)
						{
							++restored;
						}
						else
						{
							++refreshed;
						}
					}
					catch (const std::exception& error)
					{
						++rejected;
						console::warn("[images] Keeping previous %s: %s\n", name.c_str(), error.what());
					}
				}

				const auto updated = refresh_locked(cache);
				console::info(
					"[images] imageReload: %zu overrides refreshed, %zu originals restored, %zu GPU textures updated, "
					"%zu rejected, %zu without a replacement. Nonresident images update when streamed in.\n",
					refreshed, restored, updated, rejected, batch->missing);
			}
			catch (const std::exception& error)
			{
				console::error("[images] imageReload failed: %s\n", error.what());
			}
		}

		std::shared_ptr<reload_batch> collect_reload_batch(const std::string& filter)
		{
			auto batch = std::make_shared<reload_batch>();
			std::size_t resident_bytes{};
			{
				textures::graphics_lock graphics;
				auto& cache = get_cache();
				std::lock_guard lock(cache.mutex);

				for (const auto& [name, value] : cache.images)
				{
					if (utils::string::to_lower(name).find(filter) == std::string::npos)
					{
						continue;
					}

					auto& candidate = batch->images[name];
					candidate.description = value.description;
					candidate.had_override = value.data != nullptr;
				}

				for (const auto& [resource, live] : cache.textures)
				{
					if (utils::string::to_lower(live.name).find(filter) == std::string::npos
						|| (!batch->images.contains(live.name) && !files::find(live.name)))
					{
						continue;
					}

					auto& candidate = batch->images[live.name];
					candidate.description = live.description;
					auto source = live.original;

					// A first live override reads stock resident pixels on the renderer.
					// Resource ownership is protected by the cache mutex during readback.
					if (!source && !textures::stream_count(live.description))
					{
						auto image = live.description;
						image.name = live.name.c_str();
						image.texture.resource = resource;
						try
						{
							const auto mips = utils::dds::layout(
								image.width, image.height, image.mip_count, textures::dxgi_format(image));
							const auto size = mips.back().offset + mips.back().size;
							if (size > cache_limit - resident_bytes)
							{
								throw std::runtime_error("resident reload snapshot exceeds 256 MiB");
							}

							source = originals::snapshot_original(image, true);
							resident_bytes += size;
						}
						catch (...)
						{
							// An unavailable original only matters if a replacement needs changing.
						}
					}

					D3D11_TEXTURE2D_DESC desc{};
					const auto texture = textures::texture_2d(resource);
					texture->GetDesc(&desc);
					candidate.originals.push_back(
						{live.serial, std::move(source), {}, textures::stream_placeholder(live.description, desc)});
				}
			}

			return batch;
		}

		void read_original_backups(reload_candidate& candidate, std::size_t& bytes)
		{
			for (auto& original : candidate.originals)
			{
				if (original.placeholder)
				{
					continue;
				}

				// Promotion/demotion may have several resources for one source.
				for (const auto& other : candidate.originals)
				{
					if (other.data && originals::same_original(other.source, original.source))
					{
						original.data = other.data;
						break;
					}
				}

				if (original.data)
				{
					continue;
				}

				const auto stock = originals::read_original(original.source);
				if (stock->size() > cache_limit - bytes)
				{
					throw std::runtime_error("reload batch exceeds 256 MiB");
				}

				bytes += stock->size();
				original.data = stock;
			}
		}

		void read_candidate(
			const std::string& name, reload_candidate& candidate, std::size_t& bytes, std::size_t& missing)
		{
			const auto data = read_override(name);
			if (!data)
			{
				++missing;
				if (!candidate.had_override)
				{
					return;
				}

				read_original_backups(candidate, bytes);
				candidate.restore = true;

				return;
			}

			if (!matches(*data, candidate.description))
			{
				throw std::runtime_error("DDS format, dimensions or complete mip chain do not match");
			}

			if (data->size() > cache_limit - bytes)
			{
				throw std::runtime_error("reload batch exceeds 256 MiB");
			}

			bytes += data->size();
			candidate.data = data;
		}

		void read_reload_batch(const std::shared_ptr<reload_batch>& batch)
		{
			try
			{
				std::size_t bytes{};
				for (auto& [name, candidate] : batch->images)
				{
					if (stopping)
					{
						reload_busy = false;

						return;
					}

					try
					{
						read_candidate(name, candidate, bytes, batch->missing);
					}
					catch (const std::exception& error)
					{
						++batch->rejected;
						console::warn("[images] Keeping previous %s: %s\n", name.c_str(), error.what());
					}
				}

				scheduler::once(
					[batch]
					{
						apply_reload(batch);
					},
					scheduler::renderer);
			}
			catch (const std::exception& error)
			{
				reload_busy = false;
				console::error("[images] imageReload failed: %s\n", error.what());
			}
		}

		void prepare_reload(const std::string& filter)
		{
			if (stopping)
			{
				reload_busy = false;

				return;
			}

			try
			{
				const auto batch = collect_reload_batch(filter);
				console::info("[images] Reading replacement files for %zu affected images...\n", batch->images.size());
				scheduler::once([batch] { read_reload_batch(batch); }, scheduler::async);
			}
			catch (const std::exception& error)
			{
				reload_busy = false;
				console::error("[images] imageReload failed: %s\n", error.what());
			}
		}

		void reload_images(const command::params& args)
		{
			if (args.size() > 2)
			{
				console::info("imageReload [substring]\n");

				return;
			}

			if (stopping)
			{
				return;
			}

			if (reload_busy.exchange(true))
			{
				console::info("[images] An image reload is already running.\n");

				return;
			}

			try
			{
				const auto filter = utils::string::to_lower(args[1]);
				scheduler::once(
					[filter]
					{
						try
						{
							files::refresh();
							scheduler::once([filter] { prepare_reload(filter); }, scheduler::renderer);
						}
						catch (const std::exception& error)
						{
							reload_busy = false;
							console::error("[images] imageReload failed: %s\n", error.what());
						}
					},
					scheduler::async);
			}
			catch (const std::exception& error)
			{
				reload_busy = false;
				console::error("[images] imageReload failed: %s\n", error.what());
			}
		}
	}

	pixel_override find(const game::GfxImage* image, const std::optional<std::uint32_t> part) noexcept
	{
		try
		{
			return find_override(image, part);
		}
		catch (...)
		{
			// Never unwind across the engine's DB/render callback boundary.
			return {};
		}
	}

	void track_upload(const game::GfxImageTexture* texture, const game::GfxImage* image,
		const pixel_override& replacement, const std::uint8_t* original_pixels) noexcept
	{
		try
		{
			if (stopping || !texture || !texture->resource || !image || !image->name || image->map_type != 3
				|| image->depth != 1 || image->num_elements > 1)
			{
				return;
			}

			auto& cache = get_cache();
			std::lock_guard lock(cache.mutex);
			live_texture live{image->name, describe(*image)};
			live.serial = ++cache.serial;
			const auto entry = cache.images.find(live.name);
			const auto custom = entry != cache.images.end() ? entry->second.data : nullptr;
			const auto replaced = replacement.pixels != nullptr;
			if ((replaced || custom) && !textures::stream_count(*image))
			{
				// A reload can publish a replacement while native creation is in flight.
				// Retain stock bytes before either generation is applied to this resource.
				originals::capture_original(image, original_pixels);
			}

			// Pin resident originals while customized. Stream snapshots contain
			// only owned PAK descriptors, captured on the native asset's caller.
			if (replaced || custom || textures::stream_count(*image))
			{
				try
				{
					live.original = originals::snapshot_original(*image);

					// Creation may finish after a restore cleared the override cache.
					// Queue stock pixels so the older custom upload cannot undo the restore.
					if (replaced && !custom && !textures::stream_count(*image))
					{
						live.restored = originals::read_original(live.original);
					}
				}
				catch (const std::exception& error)
				{
					if (replaced || custom)
					{
						console::warn("[images] Cannot retain original %s: %s\n", image->name, error.what());
					}
				}
			}

			// The native upload already applied this generation. Only a concurrent
			// reload/restore needs another GPU upload; stock uploads need no refresh.
			live.dirty = (custom && custom != replacement.owner) || live.restored;
			if (live.dirty)
			{
				uploads_pending = true;
			}

			cache.textures.insert_or_assign(static_cast<ID3D11Resource*>(texture->resource), std::move(live));
		}
		catch (...)
		{
		}
	}

	// The native streaming copies run inside the graphics mutex. Following
	// both promotion and demotion also catches resources not published in
	// the DB yet. A queued refresh always reads the latest replacement.
	void track_copy(const game::GfxImageTexture* destination, const game::GfxImageTexture* source) noexcept
	{
		try
		{
			auto& cache = get_cache();
			std::lock_guard lock(cache.mutex);
			const auto entry = cache.textures.find(static_cast<ID3D11Resource*>(source->resource));
			if (stopping || !destination->resource || entry == cache.textures.end())
			{
				return;
			}

			auto value = entry->second;
			const auto custom = cache.images.find(value.name);
			value.dirty = value.restored || (custom != cache.images.end() && custom->second.data);
			value.serial = ++cache.serial;
			if (value.dirty)
			{
				uploads_pending = true;
			}

			cache.textures.insert_or_assign(static_cast<ID3D11Resource*>(destination->resource), std::move(value));
		}
		catch (...)
		{
		}
	}

	void release(const game::GfxImageTexture* texture)
	{
		auto& cache = get_cache();
		std::lock_guard lock(cache.mutex);
		cache.textures.erase(static_cast<ID3D11Resource*>(texture->resource));
	}

	void shutdown()
	{
		stopping = true;

		// Entries are non-owning: native teardown remains the COM owner.
		auto& cache = get_cache();
		std::lock_guard lock(cache.mutex);
		cache.textures.clear();
	}

	void initialize()
	{
		files::refresh();
		command::add("imageReload", reload_images);

		// Coalesce native uploads/copies that finish after a reload into one refresh.
		scheduler::loop(refresh_textures, scheduler::renderer);
	}
}
