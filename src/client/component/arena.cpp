#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "arena.hpp"
#include "console/console.hpp"
#include "fastfiles.hpp"
#include "filesystem.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/nt.hpp>
#include <utils/string.hpp>

namespace arena
{
	namespace
	{
		constexpr auto arena_buffer_size = 0x18000u;

		std::recursive_mutex arena_mutex;
		std::vector<std::string> generated_maps;

		bool is_valid_map_name(const std::string& name)
		{
			return !name.empty() && std::ranges::all_of(name, [](const unsigned char character)
			{
				return std::isalnum(character) != 0 || character == '_';
			});
		}

		std::vector<std::filesystem::path> get_arena_roots()
		{
			std::vector<std::filesystem::path> roots;
			for (const auto& path : filesystem::get_search_paths())
			{
				roots.emplace_back(path);
			}

			roots.emplace_back(utils::nt::library{}.get_folder());
			return roots;
		}

		void add_arena_file(const std::filesystem::path& path, std::vector<std::string>& map_names,
			std::string& arenas)
		{
			const auto map_name = utils::string::to_lower(path.stem().string());
			if (std::ranges::find(map_names, map_name) != map_names.end())
			{
				return;
			}

			std::string data;
			if (!utils::io::read_file(path.generic_string(), &data) || data.empty())
			{
				return;
			}

			console::debug("[Arena] Adding '%s'\n", path.generic_string().data());

			map_names.emplace_back(map_name);
			arenas.append("\n");
			arenas.append(data);
		}

		void add_generated_arena(const std::string& map_name, std::vector<std::string>& map_names,
			std::string& arenas)
		{
			if (std::ranges::find(map_names, map_name) != map_names.end())
			{
				return;
			}

			map_names.emplace_back(map_name);
			arenas.append(utils::string::va("\n{\n\tmap \"%s\"\n\tlongname \"%s\"\n}\n",
				map_name.data(), map_name.data()));
		}

		void for_each_file(const std::filesystem::path& directory,
			const std::function<void(const std::filesystem::directory_entry&)>& callback)
		{
			std::error_code error;
			if (!std::filesystem::is_directory(directory, error) || error)
			{
				return;
			}

			for (const auto& entry : std::filesystem::directory_iterator(directory, error))
			{
				callback(entry);
			}
		}

		std::string get_custom_arenas()
		{
			std::lock_guard _(arena_mutex);

			std::vector<std::string> map_names;
			std::string arenas;

			for (const auto& root : get_arena_roots())
			{
				// zone/<map>.arena next to the map fastfile
				for_each_file(root / "zone", [&](const std::filesystem::directory_entry& entry)
				{
					if (entry.is_regular_file() && entry.path().extension() == ".arena")
					{
						add_arena_file(entry.path(), map_names, arenas);
					}
				});

				// usermaps/<map>/<map>.arena, generated when only the fastfile is there
				for_each_file(root / "usermaps", [&](const std::filesystem::directory_entry& entry)
				{
					const auto map_name = utils::string::to_lower(entry.path().filename().string());
					if (!entry.is_directory() || !is_valid_map_name(map_name))
					{
						return;
					}

					const auto arena_path = entry.path() / (map_name + ".arena");
					if (utils::io::file_exists(arena_path.generic_string()))
					{
						add_arena_file(arena_path, map_names, arenas);
					}
					else if (utils::io::file_exists((entry.path() / (map_name + ".ff")).generic_string()))
					{
						add_generated_arena(map_name, map_names, arenas);
					}
				});
			}

			for (const auto& map_name : generated_maps)
			{
				add_generated_arena(map_name, map_names, arenas);
			}

			return arenas;
		}

		const char* read_arena_file_stub(const char* name, char* buffer, const int size)
		{
			auto* result = utils::hook::invoke<char*>(0xA7330_g, name, buffer, size); // DB_ReadRawFile
			if (!buffer || size <= 0)
			{
				return result;
			}

			if (!result)
			{
				buffer[0] = '\0';
			}

			const auto custom_arenas = get_custom_arenas();
			if (custom_arenas.empty())
			{
				return result;
			}

			const auto length = strnlen(buffer, static_cast<size_t>(size));
			const auto remaining = static_cast<size_t>(size) - length;
			if (custom_arenas.size() >= remaining)
			{
				console::warn("[Arena] Custom arenas do not fit in the arena buffer, skipping them\n");
				return result;
			}

			std::memcpy(buffer + length, custom_arenas.data(), custom_arenas.size() + 1);
			return buffer;
		}
	}

	bool register_map(const std::string& map_name)
	{
		const auto name = utils::string::to_lower(map_name);
		if (!game::environment::uses_multiplayer_binary() || !is_valid_map_name(name) || !fastfiles::exists(name))
		{
			return false;
		}

		{
			std::lock_guard _(arena_mutex);
			if (std::ranges::find(generated_maps, name) == generated_maps.end())
			{
				generated_maps.emplace_back(name);
			}
		}

		console::info("[Arena] Adding map '%s' to the arena catalog\n", name.data());

		// GameInfo_UpdateArenas only reparses the arena file when this flag is set (0x650A74)
		*game::mp::ui_arenas_dirty = 1;
		game::GameInfo_UpdateArenas();
		return true;
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::uses_multiplayer_binary())
			{
				return;
			}

			// UI_LoadArenas reading mp/basemaps.arena or mp/zmbasemaps.arena
			utils::hook::call(0x650762_g, read_arena_file_stub);
		}
	};
}

REGISTER_COMPONENT(arena::component)
