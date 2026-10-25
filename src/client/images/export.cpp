#include <std_include.hpp>

#include "export.hpp"
#include "bindings.hpp"
#include "originals.hpp"
#include "texture.hpp"
#include "game/game.hpp"
#include "game/types/materials.hpp"
#include "component/command.hpp"
#include "component/console/console.hpp"
#include "component/scheduler.hpp"
#include <utils/string.hpp>

#include <memory>

namespace images::exports
{
	namespace
	{
		struct selection
		{
			std::string query;
			bool filter{};
			bool all{};

			bool matches(const char* name) const
			{
				if (!name)
				{
					return false;
				}

				const auto lower = utils::string::to_lower(name);

				return all || (filter ? lower.find(query) != std::string::npos : lower == query);
			}
		};

		struct binding
		{
			std::string material;
			std::string image;
			std::uint32_t slot{};
			std::uint32_t semantic{};
		};

		struct material_record
		{
			std::string name;
			std::string techset;
			std::string error;
			std::vector<binding> bindings;
		};

		struct image_record
		{
			originals::image_source original;
			game::GfxImage resident_description{};
			Microsoft::WRL::ComPtr<ID3D11Resource> resident_resource;
			std::string file, error, status{"pending"};
			std::vector<binding> users;
		};

		struct export_job
		{
			selection select;
			bool materials{};
			bool list_only{};
			std::map<std::string, image_record> images;
			std::vector<material_record> selected_materials;
			std::vector<binding> all_bindings;
			std::exception_ptr failure;
			std::filesystem::path directory;
			std::filesystem::path game_directory;
			std::map<std::string, image_record>::iterator next;
			std::size_t exported{};
			std::size_t skipped{};
		};

		std::atomic_bool busy{};

		void snapshot_image(export_job& job, const game::GfxImage* image)
		{
			if (!image || !image->name)
			{
				return;
			}

			auto [it, inserted] = job.images.try_emplace(image->name);
			if (!inserted)
			{
				return;
			}

			auto& result = it->second;
			result.original.name = it->first;
			try
			{
				originals::describe(result.original, *image, false);
				if (!textures::stream_count(*image) && !result.original.resident)
				{
					// Retain the resource while DB ownership protects the image record.
					// GPU readback happens after the DB reader lock has been released.
					result.resident_resource = static_cast<ID3D11Resource*>(image->texture.resource);
					result.resident_description = *image;
					result.resident_description.name = nullptr;
					result.resident_description.pixel_data = nullptr;
					result.resident_description.texture = {};
				}
			}
			catch (const std::exception& error)
			{
				result.error = error.what();
			}
		}

		void collect_image(const game::XAssetHeader header, void* context) noexcept
		{
			auto& job = *static_cast<export_job*>(context);
			if (job.failure)
			{
				return;
			}

			try
			{
				const auto* image = static_cast<const game::GfxImage*>(header.data);
				if (image && job.select.matches(image->name))
				{
					snapshot_image(job, image);
				}
			}
			catch (...)
			{
				job.failure = std::current_exception();
			}
		}

		void collect_material(const game::XAssetHeader header, void* context) noexcept
		{
			auto& job = *static_cast<export_job*>(context);
			if (job.failure)
			{
				return;
			}

			try
			{
				const auto* material = header.material;
				if (!material || !material->name)
				{
					return;
				}

				const auto selected = job.materials && job.select.matches(material->name);
				if (job.list_only && !selected)
				{
					return;
				}

				material_record record;
				record.name = material->name;
				if (selected && material->technique_set && material->technique_set->name)
				{
					record.techset = material->technique_set->name;
				}

				if (material->image_count && !material->image_table)
				{
					record.error = "missing image binding table";
				}
				else
				{
					for (auto i = 0u; i < material->image_count; ++i)
					{
						const auto& entry = material->image_table[i];
						binding value{record.name, entry.image && entry.image->name ? entry.image->name : "", i,
							entry.semantic_hash};
						if (selected)
						{
							record.bindings.push_back(value);
							if (!job.list_only)
							{
								snapshot_image(job, entry.image);
							}
						}

						if (!job.list_only)
						{
							job.all_bindings.push_back(std::move(value));
						}
					}
				}

				if (selected)
				{
					job.selected_materials.push_back(std::move(record));
				}
			}

			// Do not unwind past DB_EnumXAssets: its reader lock must be released.
			catch (...)
			{
				job.failure = std::current_exception();
			}
		}

		void read_resident_images(export_job& job)
		{
			std::size_t bytes{};
			constexpr auto limit = std::size_t(256) * 1024 * 1024;
			for (auto& [name, image] : job.images)
			{
				if (!image.error.empty() || !image.original.parts.empty())
				{
					continue;
				}

				try
				{
					const auto& source = image.original;
					const auto mips = utils::dds::layout(source.width, source.height, source.mips, source.format);
					const auto size = mips.back().offset + mips.back().size;
					if (size > limit - bytes)
					{
						throw std::runtime_error("resident export snapshot exceeds 256 MiB; use a narrower filter");
					}

					if (!source.resident)
					{
						auto& description = image.resident_description;
						description.name = name.c_str();
						description.texture.resource = image.resident_resource.Get();
						originals::describe(image.original, description);
						description = {};
					}

					bytes += size;
				}
				catch (const std::exception& error)
				{
					image.original.resident.reset();
					image.error = error.what();
				}

				image.resident_resource.Reset();
			}
		}

		void snapshot(export_job& job)
		{
			if (!job.materials)
			{
				game::DB_EnumXAssets_FastFile(game::ASSET_TYPE_IMAGE, collect_image, &job, false);
			}

			game::DB_EnumXAssets_FastFile(game::ASSET_TYPE_MATERIAL, collect_material, &job, false);
			if (job.failure)
			{
				std::rethrow_exception(job.failure);
			}

			std::sort(job.selected_materials.begin(), job.selected_materials.end(),
				[](const auto& a, const auto& b)
				{
					return a.name < b.name;
				});
			for (auto& value : job.all_bindings)
			{
				const auto it = job.images.find(value.image);
				if (it != job.images.end())
				{
					it->second.users.push_back(std::move(value));
				}
			}

			job.all_bindings.clear();
		}

		std::string filename(const std::string& name, const std::size_t index)
		{
			// Flat filenames prevent asset names from escaping the new export directory.
			// Ordinary names stay unchanged; the manifest always records the exact asset name.
			std::string result;
			bool changed = false;
			for (const auto ch : name)
			{
				if (static_cast<unsigned char>(ch) < 32 || static_cast<unsigned char>(ch) >= 127
					|| std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos)
				{
					result += '_';
					changed = true;
				}
				else
				{
					result += ch;
				}
			}

			const auto stem = utils::string::to_lower(result.substr(0, result.find('.')));
			const auto reserved = stem == "con" || stem == "prn" || stem == "aux" || stem == "nul"
				|| (stem.size() == 4 && (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") && stem[3] >= '1'
					&& stem[3] <= '9');
			if (changed || reserved || result.empty() || result.size() > 150 || result == "." || result == ".."
				|| result.back() == '.' || result.back() == ' ')
			{
				result = "asset_" + std::to_string(index) + "_" + result.substr(0, 150);
			}

			return result + ".dds";
		}

		void write_file(const std::filesystem::path& path, const std::string_view bytes)
		{
			auto temporary = path;
			temporary += ".part";
			{
				std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
				if (!file || !file.write(bytes.data(), static_cast<std::streamsize>(bytes.size())))
				{
					throw std::runtime_error("cannot write " + temporary.string());
				}

				file.close();
				if (!file)
				{
					throw std::runtime_error("cannot finish " + temporary.string());
				}
			}

			if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			{
				throw std::runtime_error("cannot finalize " + path.string());
			}
		}

		void manifest(const export_job& job, const bool complete)
		{
			rapidjson::StringBuffer buffer;
			rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
			const auto string = [&](const char* key, const std::string& value)
			{
				writer.Key(key);
				writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
			};

			const auto number = [&](const char* key, const std::uint64_t value)
			{
				writer.Key(key);
				writer.Uint64(value);
			};

			const auto write_binding = [&](const binding& value)
			{
				writer.StartObject();
				string("material", value.material);
				string("image", value.image);
				number("slot", value.slot);
				string("semantic_hash", utils::string::va("0x%08X", value.semantic));
				const auto* role = bindings::role_name(value.semantic);
				if (role)
				{
					string("role", role);
				}
				else
				{
					writer.Key("role");
					writer.Null();
				}

				writer.EndObject();
			};

			writer.StartObject();
			number("schema_version", 1);
			string("command", job.materials ? "materialDump" : "imageDump");
			string("selection", job.select.all ? "all" : job.select.filter ? "substring" : "exact");
			string("query", job.select.query);
			string("binary_checksum", game::environment::is_singleplayer() ? "0x01743A38" : "0x020D9B94");
			writer.Key("complete");
			writer.Bool(complete);
			number("exported", job.exported);
			number("skipped", job.skipped);
			string("notes",
				"Original 2D DDS textures. Bindings include other loaded materials sharing each image. "
				"Preserve DXGI format, dimensions, channels and mip count when editing. Names are engine asset names.");

			writer.Key("materials");
			writer.StartArray();
			for (const auto& material : job.selected_materials)
			{
				writer.StartObject();
				string("name", material.name);
				if (material.techset.empty())
				{
					writer.Key("techset");
					writer.Null();
				}
				else
				{
					string("techset", material.techset);
				}

				string("error", material.error);

				writer.Key("bindings");
				writer.StartArray();
				for (const auto& value : material.bindings)
				{
					write_binding(value);
				}

				writer.EndArray();
				writer.EndObject();
			}

			writer.EndArray();

			writer.Key("images");
			writer.StartArray();
			for (const auto& [name, image] : job.images)
			{
				writer.StartObject();
				string("name", name);
				string("file", image.status == "exported" ? image.file : "");
				string("replacement_path", "s2x/images/" + name + ".dds");
				string("status", image.status);
				string("source", image.original.source);
				string("error", image.error);
				number("width", image.original.width);
				number("height", image.original.height);
				number("mip_count", image.original.mips);
				number("dxgi_format", image.original.format);
				number("map_type", image.original.map_type);
				writer.Key("native_format");
				writer.Int(image.original.native_format);

				writer.Key("parts");
				writer.StartArray();
				for (const auto& part : image.original.parts)
				{
					writer.StartObject();
					string("package", part.name);
					number("offset", part.offset);
					number("compressed_bytes", part.size);
					number("decoded_bytes", part.decoded_size);
					writer.EndObject();
				}

				writer.EndArray();

				writer.Key("bindings");
				writer.StartArray();
				for (const auto& value : image.users)
				{
					write_binding(value);
				}

				writer.EndArray();
				writer.EndObject();
			}

			writer.EndArray();
			writer.EndObject();

			write_file(job.directory / "manifest.json", {buffer.GetString(), buffer.GetSize()});
		}

		void export_image(const export_job& job, image_record& image)
		{
			if (!image.error.empty())
			{
				throw std::runtime_error(image.error);
			}

			write_file(job.directory / image.file, originals::read_dds(job.game_directory, image.original));
		}

		bool export_next_image(const std::shared_ptr<export_job>& job)
		{
			try
			{
				if (job->next != job->images.end())
				{
					auto& image = (job->next++)->second;
					try
					{
						export_image(*job, image);
						image.status = "exported";
						++job->exported;
					}
					catch (const std::exception& error)
					{
						image.status = "skipped";
						image.error = error.what();
						++job->skipped;
					}

					image.original.resident.reset();
					if (job->next != job->images.end())
					{
						return scheduler::cond_continue;
					}
				}

				manifest(*job, true);
				console::info("[images] Export complete: %zu written, %zu skipped. Manifest: %s\n", job->exported,
					job->skipped, (job->directory / "manifest.json").string().c_str());
			}
			catch (const std::exception& error)
			{
				console::error("[images] Export failed: %s\n", error.what());
			}

			busy = false;

			return scheduler::cond_end;
		}

		void start_export(const std::shared_ptr<export_job>& job)
		{
			snapshot(*job);
			read_resident_images(*job);
			job->game_directory = std::filesystem::current_path();
			const auto root = job->game_directory / "s2x/dump";
			std::filesystem::create_directories(root);
			const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch())
								   .count();
			for (auto suffix = 0u;; ++suffix)
			{
				job->directory = root
					/ (std::string(job->materials ? "materials-" : "images-") + std::to_string(stamp) + "-"
						+ std::to_string(suffix));
				if (std::filesystem::create_directory(job->directory))
				{
					break;
				}
			}

			std::filesystem::create_directory(job->directory / "images");
			std::unordered_set<std::string> paths;
			std::size_t index{};
			for (auto& [name, image] : job->images)
			{
				auto file = filename(name, ++index);
				while (!paths.insert(utils::string::to_lower(file)).second)
				{
					file = "_" + file;
				}

				image.file = "images/" + file;
			}

			manifest(*job, false);
			console::info("[images] Exporting %zu images (%zu selected materials) to %s\n", job->images.size(),
				job->selected_materials.size(), job->directory.string().c_str());
			job->next = job->images.begin();

			// No engine pointers or graphics resources escape the snapshot. One image per
			// async tick keeps the scheduler responsive, even when exporting all images.
			scheduler::schedule(
				[job]
				{
					return export_next_image(job);
				},
				scheduler::async);
		}

		void dump_command(const command::params& args, const bool materials)
		{
			const std::string_view value = args[1];
			selection select;
			if (args.size() == 2 && value == "all")
			{
				select.all = true;
			}
			else if (args.size() == 3 && value == "filter" && *args[2])
			{
				select.filter = true;
				select.query = utils::string::to_lower(args[2]);
			}
			else if (args.size() == 2 && !value.empty() && value != "filter" && !value.starts_with("--"))
			{
				select.query = utils::string::to_lower(args[1]);
			}
			else
			{
				console::info(
					"%s <exact-name> | filter <substring> | all\n", materials ? "materialDump" : "imageDump");

				return;
			}

			if (busy.exchange(true))
			{
				console::warn("[images] An export is already running; wait for its completion message.\n");

				return;
			}

			try
			{
				auto job = std::make_shared<export_job>();
				job->select = std::move(select);
				job->materials = materials;
				scheduler::once(
					[job]
					{
						try
						{
							start_export(job);
						}
						catch (const std::exception& error)
						{
							busy = false;
							console::error("[images] Cannot start export: %s\n", error.what());
						}
					},
					scheduler::renderer);
			}
			catch (...)
			{
				busy = false;
				console::error("[images] Cannot schedule export.\n");
			}
		}

		struct image_list
		{
			std::string filter;
			std::vector<std::string> rows;
			std::exception_ptr failure;
		};

		void collect_image_row(const game::XAssetHeader header, void* data) noexcept
		{
			auto& list = *static_cast<image_list*>(data);
			if (list.failure)
			{
				return;
			}

			try
			{
				const auto* image = static_cast<const game::GfxImage*>(header.data);
				if (!image || !image->name
					|| utils::string::to_lower(image->name).find(list.filter) == std::string::npos)
				{
					return;
				}

				auto width = std::uint32_t(image->width);
				auto height = std::uint32_t(image->height);
				auto mips = std::uint32_t(image->mip_count);
				const auto parts = textures::stream_count(*image);
				for (auto i = 0u; i < parts; ++i)
				{
					const auto& stream = image->streams[i];
					width = std::max(width, std::uint32_t(stream.width));
					height = std::max(height, std::uint32_t(stream.height));
					mips = std::max(mips, stream.mip_count());
				}

				const auto format = textures::dxgi_format(*image);

				// Copy the text under the DB reader lock; print only after enumeration.
				list.rows.emplace_back(utils::string::va("%s | %ux%u | %u mips | %s (DXGI %u) | type %u | %s", image->name,
					width, height, mips, textures::format_name(format), format, unsigned(image->map_type),
					parts ? "streamed" : "resident"));
			}
			catch (...)
			{
				list.failure = std::current_exception();
			}
		}

		void list_images(const command::params& args)
		{
			if (args.size() > 2)
			{
				console::info("imageList [substring]\n");

				return;
			}

			try
			{
				image_list list{utils::string::to_lower(args[1]), {}, {}};
				game::DB_EnumXAssets_FastFile(game::ASSET_TYPE_IMAGE, collect_image_row, &list, false);
				if (list.failure)
				{
					std::rethrow_exception(list.failure);
				}

				std::sort(list.rows.begin(), list.rows.end());
				for (const auto& row : list.rows)
				{
					console::info("%s\n", row.c_str());
				}

				console::info("%zu loaded images. imageList [substring]; replacements: s2x/images/<name>.dds. "
							  "Use imageReload [substring] after changing files.\n",
					list.rows.size());
			}
			catch (const std::exception& error)
			{
				console::error("[images] imageList failed: %s\n", error.what());
			}
		}

		void list_materials(const command::params& args)
		{
			if (args.size() > 2)
			{
				console::info("materialList [substring]\n");

				return;
			}

			try
			{
				export_job job;
				job.materials = job.list_only = true;
				job.select = {utils::string::to_lower(args[1]), true, false};
				snapshot(job);
				for (const auto& material : job.selected_materials)
				{
					console::info("%s | %zu image bindings %s\n", material.name.c_str(), material.bindings.size(),
						material.error.c_str());
					for (const auto& value : material.bindings)
					{
						console::info("  %s\n", bindings::format(value.slot, value.semantic, value.image.c_str()).c_str());
					}
				}

				console::info("%zu loaded materials. materialList [substring]\n", job.selected_materials.size());
			}
			catch (const std::exception& error)
			{
				console::error("[images] materialList failed: %s\n", error.what());
			}
		}
	}

	void register_commands()
	{
		command::add("imageList", list_images);
		command::add("imageDump",
			[](const command::params& args)
			{
				dump_command(args, false);
			});
		command::add("materialDump",
			[](const command::params& args)
			{
				dump_command(args, true);
			});
		command::add("materialList", list_materials);
	}
}
