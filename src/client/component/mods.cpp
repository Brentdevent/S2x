#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/ui_scripting/types.hpp"

#include "filesystem.hpp"
#include "mods.hpp"
#include "scheduler.hpp"
#include "ui_scripting.hpp"

#include "console/console.hpp"

#include <utils/io.hpp>
#include <utils/nt.hpp>

namespace mods
{
	namespace
	{
		std::string loaded_mod;
		std::filesystem::path loaded_mod_path;

		std::filesystem::path get_mods_directory()
		{
			static const auto path = std::filesystem::path{ utils::nt::library{}.get_folder() } / "s2x" / "mods";
			return path;
		}

		bool is_safe_mod_name(const std::string& name)
		{
			return !name.empty() && name != "." && name != ".." &&
				name.find_first_of("/\\:") == std::string::npos &&
				name.find('\0') == std::string::npos;
		}

		bool is_supported_mod_folder(const std::filesystem::path& path)
		{
			std::error_code error;
			if (!std::filesystem::is_directory(path, error) || error)
			{
				return false;
			}

			error.clear();
			const auto has_scripts = std::filesystem::is_directory(path / "scripts", error) && !error;
			error.clear();
			const auto has_ui_scripts = std::filesystem::is_directory(path / "ui_scripts", error) && !error;
			
			return has_scripts || has_ui_scripts;
		}

		std::filesystem::path get_mod_path(const std::string& name)
		{
			return get_mods_directory() / name;
		}

		bool can_change_mods()
		{
			return !game::environment::is_dedicated() &&
				(game::environment::is_multiplayer() || game::environment::is_zombies()) &&
				game::virtual_lobby_loaded();
		}

		bool supports_current_mode(const mod_info& info)
		{
			if (game::environment::is_zombies())
			{
				return info.supports_zm;
			}

			return game::environment::is_multiplayer() && info.supports_mp;
		}

		void invalidate_mode(mod_info& info)
		{
			info.supports_mp = false;
			info.supports_zm = false;
			info.mode.clear();
		}

		std::string read_string_member(const rapidjson::Value& object, const char* key,
			const std::string& fallback = {})
		{
			const auto found = object.FindMember(key);
			if (found != object.MemberEnd() && found->value.IsString())
			{
				return { found->value.GetString(), found->value.GetStringLength() };
			}

			return fallback;
		}

		bool parse_mode(const rapidjson::Value& document, mod_info& info)
		{
			const auto found = document.FindMember("mode");
			if (found == document.MemberEnd())
			{
				return true;
			}

			if (!found->value.IsString())
			{
				return false;
			}

			const std::string value{ found->value.GetString(), found->value.GetStringLength() };
			bool supports_mp = false;
			bool supports_zm = false;
			std::size_t start = 0;

			while (start <= value.size())
			{
				const auto separator = value.find(',', start);
				const auto length = separator == std::string::npos ? std::string::npos : separator - start;
				auto mode = value.substr(start, length);
				
				while (!mode.empty() && std::isspace(static_cast<unsigned char>(mode.front())))
				{
					mode.erase(mode.begin());
				}
				
				while (!mode.empty() && std::isspace(static_cast<unsigned char>(mode.back())))
				{
					mode.pop_back();
				}
				
				std::ranges::transform(mode, mode.begin(), [](const unsigned char character)
				{
					return static_cast<char>(std::tolower(character));
				});

				if (mode == "mp")
				{
					supports_mp = true;
				}
				else if (mode == "zm")
				{
					supports_zm = true;
				}
				else
				{
					return false;
				}

				if (separator == std::string::npos)
				{
					break;
				}
				
				start = separator + 1;
			}

			if (!supports_mp && !supports_zm)
			{
				return false;
			}

			info.supports_mp = supports_mp;
			info.supports_zm = supports_zm;
			info.mode = supports_mp && supports_zm ? "mp, zm" : supports_mp ? "mp" : "zm";
			
			return true;
		}

		void mount_mod(const std::string& name)
		{
			if (!loaded_mod_path.empty())
			{
				filesystem::unregister_path(loaded_mod_path);
				loaded_mod_path.clear();
			}

			loaded_mod.clear();
			if (name.empty())
			{
				return;
			}

			loaded_mod = name;
			loaded_mod_path = get_mod_path(name);
			filesystem::register_path(loaded_mod_path);
		}

		void restart_lui()
		{
			scheduler::once([]()
			{
				game::LUI_CoD_Restart(true);
			}, scheduler::main);
		}

		void initialize_mods()
		{
			(void)filesystem::get_search_paths();
			utils::io::create_directory(get_mods_directory());
			utils::io::remove_file(get_mods_directory() / ".active_mod");
		}

		bool directory_has_entries(const std::filesystem::path& path)
		{
			std::error_code error;
			if (!std::filesystem::is_directory(path, error))
			{
				if (error && error != std::errc::no_such_file_or_directory)
				{
					console::warn("[Mods] Could not inspect '%s': %s\n", path.string().data(), error.message().data());
				}
				return false;
			}

			std::filesystem::directory_iterator it(path, error);
			if (error)
			{
				if (error != std::errc::no_such_file_or_directory)
				{
					console::warn("[Mods] Could not inspect '%s': %s\n", path.string().data(), error.message().data());
				}
				return false;
			}

			return it != std::filesystem::directory_iterator{};
		}
	}

	std::string get_loaded_mod()
	{
		return loaded_mod;
	}

	std::vector<std::string> get_mod_list()
	{
		std::vector<std::string> result;
		const auto root = get_mods_directory();
		std::error_code error;
		if (!std::filesystem::is_directory(root, error) || error)
		{
			return result;
		}

		for (std::filesystem::directory_iterator it(root, error), end; !error && it != end; it.increment(error))
		{
			std::error_code entry_error;
			if (!it->is_directory(entry_error) || entry_error)
			{
				continue;
			}

			const auto name = it->path().filename().string();
			if (is_safe_mod_name(name) && is_supported_mod_folder(it->path()) &&
				supports_current_mode(get_mod_info(name)))
			{
				result.push_back(name);
			}
		}

		std::ranges::sort(result);
		return result;
	}

	mod_info get_mod_info(const std::string& name)
	{
		mod_info info{};
		info.name = name;
		info.description = "No description provided.";

		if (!is_safe_mod_name(name))
		{
			return info;
		}

		const auto path = get_mod_path(name);
		if (!is_supported_mod_folder(path))
		{
			return info;
		}

		std::string data;
		if (!utils::io::read_file((path / "meta.json").string(), &data))
		{
			return info;
		}

		rapidjson::Document document;
		document.Parse(data.c_str());
		if (document.HasParseError() || !document.IsObject())
		{
			invalidate_mode(info);
			return info;
		}

		if (!parse_mode(document, info))
		{
			invalidate_mode(info);
			return info;
		}

		info.valid = true;
		info.name = read_string_member(document, "name", name);
		info.description = read_string_member(document, "description", info.description);
		info.author = read_string_member(document, "author");
		info.version = read_string_member(document, "version");
		
		return info;
	}

	void load(const std::string& name)
	{
		if (!can_change_mods())
		{
			console::warn("[Mods] Mods can only be changed from the Multiplayer or Zombies lobby, before a match starts.\n");
			return;
		}

		if (!is_safe_mod_name(name) || !is_supported_mod_folder(get_mod_path(name)))
		{
			console::warn("[Mods] '%s' is not a valid S2x mod folder. Expected s2x/mods/<name>/scripts or ui_scripts.\n", name.data());
			return;
		}

		if (!supports_current_mode(get_mod_info(name)))
		{
			const auto mode = game::environment::is_zombies() ? "zm" : "mp";
			console::warn("[Mods] '%s' is not configured for %s mode in meta.json.\n", name.data(), mode);
			return;
		}

		if (loaded_mod == name)
		{
			return;
		}

		mount_mod(name);
		restart_lui();
	}

	void unload()
	{
		if (!can_change_mods())
		{
			console::warn("[Mods] Mods can only be changed from the Multiplayer or Zombies lobby, before a match starts.\n");
			return;
		}

		if (loaded_mod.empty())
		{
			return;
		}

		mount_mod("");
		restart_lui();
	}

	void install_lua_api()
	{
		const auto lua = ui_scripting::get_globals();
		auto mods_table = ui_scripting::table{};
		lua["mods"] = mods_table;

		mods_table["getloaded"] = []()
		{
			return mods::get_loaded_mod();
		};

		mods_table["getloosefiles"] = []()
		{
			const auto s2x_directory = std::filesystem::path{ utils::nt::library{}.get_folder() } / "s2x";
			ui_scripting::table loose_files{};
			loose_files["scripts"] = directory_has_entries(s2x_directory / "scripts");
			loose_files["ui_scripts"] = directory_has_entries(s2x_directory / "ui_scripts");
			return loose_files;
		};

		mods_table["getlist"] = []()
		{
			return mods::get_mod_list();
		};

		mods_table["getinfo"] = [](const std::string& name)
		{
			const auto metadata = mods::get_mod_info(name);
			ui_scripting::table info{};
			info["isvalid"] = metadata.valid;
			info["name"] = metadata.name;
			info["description"] = metadata.description;
			info["author"] = metadata.author;
			info["version"] = metadata.version;
			info["mode"] = metadata.mode;
			return info;
		};

		mods_table["load"] = [](const std::string& name)
		{
			scheduler::once([name]()
			{
				mods::load(name);
			}, scheduler::main);
		};

		mods_table["unload"] = []()
		{
			scheduler::once([]()
			{
				mods::unload();
			}, scheduler::main);
		};
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated() ||
				(!game::environment::is_multiplayer() && !game::environment::is_zombies()))
			{
				return;
			}

			initialize_mods();

			ui_scripting::on_start(install_lua_api);
		}
	};
}

REGISTER_COMPONENT(mods::component)
