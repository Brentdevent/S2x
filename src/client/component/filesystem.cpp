#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "filesystem.hpp"
#include "console/console.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/nt.hpp>

namespace filesystem
{
	namespace
	{
		utils::hook::detour fs_startup_hook;

		bool initialized = false;
		bool custom_path_registered = false;

		std::vector<std::function<void(const std::string&, const std::string&)>> exec_file_callbacks{};
		std::vector<std::function<bool(const std::string&, std::string&, std::size_t)>> exec_file_transforms{};

		void notify_exec_file_read(const char* filename, const char* data)
		{
			for (const auto& callback : exec_file_callbacks)
			{
				callback(filename, data);
			}
		}

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

		// Applies the registered exec transforms to config text that is about to
		// enter the engine's exec buffer of `size` bytes. A rewrite that would not
		// fit is the transform's own problem to refuse; this only guards the copy.
		bool rewrite_exec_text(const char* filename, std::string& text, const int size)
		{
			if (exec_file_transforms.empty())
			{
				return false;
			}

			auto rewritten = text;
			auto changed = false;
			for (const auto& transform : exec_file_transforms)
			{
				changed = transform(filename, rewritten, static_cast<std::size_t>(size)) || changed;
			}

			if (!changed)
			{
				return false;
			}

			if (rewritten.size() >= static_cast<std::size_t>(size))
			{
				console::warn("Config file '%s' was rewritten past the exec buffer size (%zu of %d bytes); executing it as written.\n",
					filename, rewritten.size(), size);
				return false;
			}

			text = std::move(rewritten);
			return true;
		}

		char* read_raw_file_for_exec_stub(const char* filename, char* buffer, const int size)
		{
			if (!filename || !buffer || size <= 0)
			{
				return nullptr;
			}

			if (auto* result = game::DB_ReadRawFile(filename, buffer, size))
			{
				// A packaged config the engine copied into our buffer can be rewritten
				// like a loose one; a pointer into asset memory is left alone.
				if (result == buffer)
				{
					std::string text(buffer);
					if (rewrite_exec_text(filename, text, size))
					{
						std::memcpy(buffer, text.data(), text.size());
						buffer[text.size()] = '\0';
					}
				}

				notify_exec_file_read(filename, result);
				return result;
			}

			char* loose_buffer{};
			const auto length = game::FS_ReadFile(filename, &loose_buffer);
			if (length < 0 || !loose_buffer)
			{
				return nullptr;
			}

			std::string text(loose_buffer, static_cast<std::size_t>(length));
			game::FS_FreeFile(loose_buffer);

			if (text.size() >= static_cast<std::size_t>(size))
			{
				console::error("Config file '%s' exceeds the exec buffer size.\n", filename);
				return nullptr;
			}

			rewrite_exec_text(filename, text, size);

			std::memcpy(buffer, text.data(), text.size());
			buffer[text.size()] = '\0';
			notify_exec_file_read(filename, buffer);
			return buffer;
		}

		std::deque<std::filesystem::path>& get_search_paths_internal()
		{
			static std::deque<std::filesystem::path> search_paths{};
			return search_paths;
		}

		bool can_insert_path(const std::filesystem::path& path)
		{
			const auto normalized = path.lexically_normal();

			for (const auto& path_ : get_search_paths_internal())
			{
				if (path_.lexically_normal() == normalized)
				{
					return false;
				}
			}

			return true;
		}

		void startup()
		{
			if (initialized)
			{
				return;
			}

			initialized = true;

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
		}

		void check_for_startup()
		{
			if (!initialized)
			{
				startup();
			}
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

		check_for_startup();

		for (const auto& search_path : get_search_paths_internal())
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
		check_for_startup();

		for (const auto& search_path : get_search_paths_internal())
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

	void on_exec_file_read(const std::function<void(const std::string& name, const std::string& data)>& callback)
	{
		exec_file_callbacks.push_back(callback);
	}

	void on_exec_file_transform(
		const std::function<bool(const std::string& name, std::string& data, std::size_t capacity)>& callback)
	{
		exec_file_transforms.push_back(callback);
	}

	void register_path(const std::filesystem::path& path)
	{
		const auto normalized = path.lexically_normal();

		if (can_insert_path(normalized))
		{
			console::debug("[FS] Registering path '%s'\n", normalized.generic_string().data());
			get_search_paths_internal().push_front(normalized);
		}
	}

	void unregister_path(const std::filesystem::path& path)
	{
		check_for_startup();

		const auto normalized = path.lexically_normal();
		auto& search_paths = get_search_paths_internal();

		for (auto i = search_paths.begin(); i != search_paths.end();)
		{
			if (i->lexically_normal() == normalized)
			{
				console::debug("[FS] Unregistering path '%s'\n", i->generic_string().data());
				i = search_paths.erase(i);
			}
			else
			{
				++i;
			}
		}
	}

	std::vector<std::string> get_search_paths()
	{
		check_for_startup();

		std::vector<std::string> paths{};

		for (const auto& path : get_search_paths_internal())
		{
			paths.push_back(path.generic_string());
		}

		return paths;
	}

	std::vector<std::string> get_search_paths_rev()
	{
		check_for_startup();

		std::vector<std::string> paths{};
		const auto& search_paths = get_search_paths_internal();

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
