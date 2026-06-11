#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "scheduler.hpp"
#include "command.hpp"
#include "console/console.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"
#include "game/ui_scripting/execution.hpp"

#include "ui_scripting.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/finally.hpp>

namespace ui_scripting
{
	namespace
	{
		std::unordered_map<game::hks::cclosure*, std::function<arguments(const function_arguments& args)>> converted_functions;

		utils::hook::detour hks_start_hook;
		utils::hook::detour hks_shutdown_hook;
		utils::hook::detour hks_package_require_hook;

		struct script
		{
			std::string name;
			std::string root;
		};

		struct globals_s
		{
			std::string in_require_script;
			std::vector<script> loaded_scripts;
			bool load_raw_script{};
			std::string raw_script_name{};
		};

		globals_s globals{};

		bool is_loaded_script(const std::string& name)
		{
			return std::ranges::any_of(globals.loaded_scripts, [name](const auto& loaded_script)
			{
				return loaded_script.name == name;
			});
		}

		std::string get_root_script(const std::string& name)
		{
			for (const auto& loaded_script : globals.loaded_scripts)
			{
				if (loaded_script.name == name)
				{
					return loaded_script.root;
				}
			}

			return {};
		}

		table get_globals()
		{
			const auto state = *game::hks::lua_state;
			return state->globals.v.table;
		}

		void print_error(const std::string& error)
		{
			console::error("************** LUI script execution error **************\n");
			console::error("%s\n", error.data());
			console::error("********************************************************\n");
		}

		void print_loading_script(const std::string& name)
		{
			console::info("Loading LUI script '%s'\n", name.data());
		}

		std::string get_current_script(game::hks::lua_State* state)
		{
			game::hks::lua_Debug info{};
			game::hks::hksi_lua_getstack(state, 1, &info);
			game::hks::hksi_lua_getinfo(state, "nSl", &info);
			return info.short_src;
		}

		int load_buffer(const std::string& name, const std::string& data)
		{
			const auto state = *game::hks::lua_state;
			const auto sharing_mode = state->m_global->m_bytecodeSharingMode;
			state->m_global->m_bytecodeSharingMode = game::hks::HKS_BYTECODE_SHARING_ON;
			const auto _0 = utils::finally([&]()
			{
				state->m_global->m_bytecodeSharingMode = sharing_mode;
			});

			game::hks::HksCompilerSettings compiler_settings{};
			return game::hks::hksi_hksL_loadbuffer(state, &compiler_settings, data.data(), static_cast<unsigned int>(data.size()), name.data());
		}

		void load_script(const std::string& name, const std::string& data)
		{
			globals.loaded_scripts.push_back({name, name});

			const auto lua = get_globals();
			const auto load_results = lua["loadstring"](data, name);

			if (load_results[0].is<function>())
			{
				const auto results = lua["pcall"](load_results);
				if (!results[0].as<bool>())
				{
					print_error(results[1].as<std::string>());
				}
			}
			else if (load_results[1].is<std::string>())
			{
				print_error(load_results[1].as<std::string>());
			}
		}

		void load_scripts(const std::string& script_dir)
		{
			if (!utils::io::directory_exists(script_dir))
			{
				return;
			}

			const auto scripts = utils::io::list_files(script_dir);

			for (const auto& script : scripts)
			{
				std::string data{};
				if (std::filesystem::is_directory(script) && utils::io::read_file(script / "__init__.lua", &data))
				{
					print_loading_script(script.string());
					load_script((script / "__init__.lua").string(), data);
				}
			}
		}

		void setup_functions()
		{
			const auto lua = get_globals();

			using game = table;
			auto game_type = game();
			lua["game"] = game_type;

			game_type["issingleplayer"] = [](const game&)
			{
				return ::game::environment::is_sp();
			};

			game_type["ismultiplayer"] = [](const game&)
			{
				return ::game::environment::is_mp();
			};
		}

		void enable_globals()
		{
			const auto lua = get_globals();
			const std::string script =
				"local g = getmetatable(_G)\n"
				"if not g then\n"
					"g = {}\n"
					"setmetatable(_G, g)\n"
				"end\n"
				"g.__newindex = nil\n";

			(void)lua["loadstring"](script)[0]();
		}

		void start()
		{
			globals = {};
			const auto lua = get_globals();

			enable_globals();
			setup_functions();

			lua["print"] = function(reinterpret_cast<game::hks::lua_function>(game::hks::base_print.get()));
			lua["table"]["unpack"] = lua["unpack"];
			lua["luiglobals"] = lua;

			load_scripts((game::get_appdata_path() / "data/ui_scripts/").string());
			load_scripts("s2x/ui_scripts/");
		}

		void try_start()
		{
			try
			{
				start();
			}
			catch (const std::exception& e)
			{
				console::error("Failed to load LUI scripts: %s\n", e.what());
			}
		}

		void* hks_start_stub(char a1)
		{
			const auto _0 = utils::finally(&try_start);
			return hks_start_hook.invoke<void*>(a1);
		}

		void hks_shutdown_stub()
		{
			converted_functions.clear();
			globals = {};
			return hks_shutdown_hook.invoke<void>();
		}

		int hks_package_require_stub(game::hks::lua_State* state)
		{
			const auto script = get_current_script(state);
			const auto root = get_root_script(script);
			globals.in_require_script = root;
			return hks_package_require_hook.invoke<int>(state);
		}

		game::XAssetHeader db_find_x_asset_header_stub(game::XAssetType type, const char* name, int allow_create_default)
		{
			game::XAssetHeader header{.luaFile = nullptr};

			if (!is_loaded_script(globals.in_require_script))
			{
				return game::DB_FindXAssetHeader(type, name, allow_create_default);
			}

			const auto folder = globals.in_require_script.substr(0, globals.in_require_script.find_last_of("/\\"));
			const std::string name_ = name;
			const std::string target_script = folder + "/" + name_ + ".lua";

			if (utils::io::file_exists(target_script))
			{
				globals.load_raw_script = true;
				globals.raw_script_name = target_script;
				header.luaFile = reinterpret_cast<game::LuaFile*>(1);
			}
			else if (name_.starts_with("ui/LUI/"))
			{
				return game::DB_FindXAssetHeader(type, name, allow_create_default);
			}

			return header;
		}

		int hks_load_stub(game::hks::lua_State* state, void* compiler_options, 
			void* reader, void* reader_data, const char* chunk_name)
		{
			if (globals.load_raw_script)
			{
				globals.load_raw_script = false;
				globals.loaded_scripts.push_back({globals.raw_script_name, globals.in_require_script});
				return load_buffer(globals.raw_script_name, utils::io::read_file(globals.raw_script_name));
			}

			return game::hks::load(state, compiler_options, reader, reader_data, chunk_name);
		}

		int main_handler(game::hks::lua_State* state)
		{
			const auto value = state->m_apistack.base[-1];
			if (value.t != game::hks::TCFUNCTION)
			{
				return 0;
			}

			const auto closure = value.v.cClosure;
			if (!converted_functions.contains(closure))
			{
				return 0;
			}

			const auto& function = converted_functions[closure];

			try
			{
				const auto args = get_return_values();
				const auto results = function(args);

				for (const auto& result : results)
				{
					push_value(result);
				}

				return static_cast<int>(results.size());
			}
			catch (const std::exception& e)
			{
				game::hks::hksi_luaL_error(state, e.what());
			}

			return 0;
		}

		int hks_base_printf_stub(const char* fmt, ...)
		{
			char buffer[4096]{};

			va_list args;
			va_start(args, fmt);
			const auto len = vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, args);
			va_end(args);

			if (len > 0)
			{
				console::info("[LUI] %s\n", buffer);
			}

			return len;
		}
	}

	table get_globals()
	{
		const auto state = *game::hks::lua_state;
		return state->globals.v.table;
	}

	template <typename F>
	game::hks::cclosure* convert_function(F f)
	{
		const auto closure = game::hks::cclosure_Create(main_handler);
		converted_functions[closure] = wrap_function(f);
		return closure;
	}

	class component final : public generic_component
	{
	public:

		void post_unpack() override
		{
			utils::hook::call(game::select(0x309B7B, 0x17E16B), db_find_x_asset_header_stub);
			utils::hook::call(game::select(0x309CBC, 0x17E2AC), db_find_x_asset_header_stub);
			utils::hook::call(game::select(0x309CFF, 0x17E2EF), hks_load_stub);
			utils::hook::call(game::select(0x2BA4B7, 0x12E877), hks_base_printf_stub);

			hks_package_require_hook.create(game::hks::package_require, hks_package_require_stub);
			hks_start_hook.create(game::LUI_CoD_Init, hks_start_stub);
			hks_shutdown_hook.create(game::LUI_CoD_Shutdown, hks_shutdown_stub);

			command::add("lui_restart", []()
			{
				game::LUI_CoD_Restart(true);
			});

			// TODO: Patch unsafe LUA functions
		}
	};
}

REGISTER_COMPONENT(ui_scripting::component)
