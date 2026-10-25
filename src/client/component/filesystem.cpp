#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "filesystem.hpp"
#include "console/console.hpp"

#include "game/game.hpp"

#include <utils/concurrency.hpp>
#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/nt.hpp>

namespace filesystem
{
	namespace
	{
		utils::hook::detour fs_startup_hook;

		bool custom_path_registered = false;
		std::atomic_uint64_t path_version{};

		void register_custom_path()
		{
			if (custom_path_registered)
			{
				return;
			}

			custom_path_registered = true;

			const auto game_dir = utils::nt::library{}.get_folder().string();
			const auto appdata_dir = game::get_appdata_path().string();

			game::FS_AddLocalizedGameDirectory(game_dir.data(), "s2x");
			game::FS_AddLocalizedGameDirectory(appdata_dir.data(), "data");
		}

		void fs_startup_stub(const char* game_name)
		{
			custom_path_registered = false;
			fs_startup_hook.invoke<void>(game_name);
		}

		void register_custom_path_stub(const char* path, const char* dir)
		{
			register_custom_path();
			game::FS_AddLocalizedGameDirectory(path, dir);
		}

		char* read_raw_file_for_exec_stub(const char* filename, char* buffer, const int size)
		{
			if (!filename || !buffer || size <= 0)
			{
				return nullptr;
			}

			// A dedicated server takes its gameplay values from the admin config, not from a
			// playlist. The stock lobby flow re-executes default_mp_allmodes.cfg (through
			// default_xboxlive.cfg) when the party is created, when a match ends and when the
			// lobby returns; each run put every gameplay dvar back to its stock value, which is
			// what reset admin settings on every rotation. Execute it once, at startup, ahead
			// of the admin config, and leave the admin values alone afterwards.
			if (game::environment::is_dedicated() && game::environment::is_multiplayer()
				&& std::string_view{filename} == "default_mp_allmodes.cfg")
			{
				static bool defaults_applied = false;
				if (defaults_applied)
				{
					buffer[0] = 0;
					return buffer;
				}
				defaults_applied = true;
			}

			if (auto* result = game::DB_ReadRawFile(filename, buffer, size))
			{
				return result;
			}

			char* loose_buffer{};
			const auto length = game::FS_ReadFile(filename, &loose_buffer);
			if (length < 0 || !loose_buffer)
			{
				return nullptr;
			}

			if (length >= size)
			{
				game::FS_FreeFile(loose_buffer);
				console::error("Config file '%s' exceeds the exec buffer size.\n", filename);
				return nullptr;
			}

			std::memcpy(buffer, loose_buffer, static_cast<std::size_t>(length));
			buffer[length] = '\0';
			game::FS_FreeFile(loose_buffer);
			return buffer;
		}

		utils::concurrency::container<std::deque<std::filesystem::path>>& get_search_paths_internal()
		{
			static utils::concurrency::container<std::deque<std::filesystem::path>> search_paths{};
			return search_paths;
		}

		void startup()
		{
			static std::once_flag initialized;
			std::call_once(initialized, []()
			{
				const auto base = std::filesystem::path{
					utils::nt::library{}.get_folder()
				};

				register_path(base / "main");
				register_path(base / "raw");
				register_path(base / "raw_shared");
				register_path(base / "devraw_shared");
				register_path(base / "devraw");

				// Client custom folders
				register_path(base / "s2x");
				register_path(game::get_appdata_path() / "data");
			});
		}

		std::deque<std::filesystem::path> snapshot_search_paths()
		{
			startup();

			// Mod changes can overlap DB/async reads. Copy under the lock, then do I/O without it.
			return get_search_paths_internal().copy();
		}
	}

	std::string read_file(const std::string& path)
	{
		std::string data{};
		read_file(path, &data);
		return data;
	}

	bool read_file(const std::string& path, std::string* data, std::string* real_path)
	{
		if (!data)
		{
			return false;
		}

		for (const auto& search_path : snapshot_search_paths())
		{
			const auto path_ = search_path / path;

			if (utils::io::read_file(path_.generic_string(), data))
			{
				if (real_path)
				{
					*real_path = path_.generic_string();
				}

				return true;
			}
		}

		return false;
	}

	bool find_file(const std::string& path, std::string* real_path)
	{
		for (const auto& search_path : snapshot_search_paths())
		{
			const auto path_ = search_path / path;

			if (utils::io::file_exists(path_.generic_string()))
			{
				if (real_path)
				{
					*real_path = path_.generic_string();
				}

				return true;
			}
		}

		return false;
	}

	bool exists(const std::string& path)
	{
		std::string real_path{};
		return find_file(path, &real_path);
	}

	void register_path(const std::filesystem::path& path)
	{
		const auto normalized = path.lexically_normal();
		const auto inserted = get_search_paths_internal().access<bool>([&](auto& paths)
		{
			if (std::find(paths.begin(), paths.end(), normalized) != paths.end())
			{
				return false;
			}

			paths.push_front(normalized);
			++path_version;

			return true;
		});

		if (inserted)
		{
			console::debug("[FS] Registering path '%s'\n", normalized.generic_string().data());
		}
	}

	void unregister_path(const std::filesystem::path& path)
	{
		startup();

		const auto normalized = path.lexically_normal();
		const auto removed = get_search_paths_internal().access<bool>([&](auto& paths)
		{
			if (!std::erase(paths, normalized))
			{
				return false;
			}

			++path_version;

			return true;
		});

		if (removed)
		{
			console::debug("[FS] Unregistering path '%s'\n", normalized.generic_string().data());
		}
	}

	std::uint64_t search_paths_version()
	{
		return path_version.load();
	}

	std::vector<std::string> get_search_paths()
	{
		std::vector<std::string> paths{};

		for (const auto& path : snapshot_search_paths())
		{
			paths.push_back(path.generic_string());
		}

		return paths;
	}

	std::vector<std::string> get_search_paths_rev()
	{
		std::vector<std::string> paths{};
		const auto search_paths = snapshot_search_paths();

		for (auto i = search_paths.rbegin(); i != search_paths.rend(); ++i)
		{
			paths.push_back(i->generic_string());
		}

		return paths;
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			startup();

			// Register the custom directories in the engine search path on every FS startup.
			fs_startup_hook.create(game::FS_Startup, fs_startup_stub);
			utils::hook::call(game::select(0x757FCD, 0x4C0A8D), register_custom_path_stub);
			utils::hook::call(game::select(0x757FE0, 0x4C0AA0), register_custom_path_stub);
			utils::hook::call(game::select(0x75803E, 0x4C0AFE), register_custom_path_stub);
			utils::hook::call(game::select(0x75807D, 0x4C0B3D), register_custom_path_stub);
			utils::hook::call(game::select(0x7580DB, 0x4C0B9B), register_custom_path_stub);
			utils::hook::call(game::select(0x75811A, 0x4C0BDA), register_custom_path_stub);

			// Initial FS startup has completed before component post-unpack callbacks run.
			register_custom_path();

			// Generic exec reads packaged RawFile assets first; allow loose configs as a fallback.
			utils::hook::call(game::select(0x64AF66, 0x465864), read_raw_file_for_exec_stub);
		}
	};
}

REGISTER_COMPONENT(filesystem::component)
