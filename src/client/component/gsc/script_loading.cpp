#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"

#include <utils/compression.hpp>
#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/memory.hpp>

#include "component/console/console.hpp"
#include "component/scripting.hpp"
#include "component/filesystem.hpp"

#include "script_extension.hpp"
#include "script_loading.hpp"

namespace gsc
{
	std::unique_ptr<xsk::gsc::s2::context> gsc_ctx;

	namespace
	{
		std::unordered_map<std::string, unsigned int> main_handles;
		std::unordered_map<std::string, unsigned int> init_handles;

		std::unordered_map<std::string, game::ScriptFile*> loaded_scripts;
		utils::memory::allocator script_allocator;

		std::uintptr_t custom_script_code_cursor = 0;

		void clear()
		{
			main_handles.clear();
			init_handles.clear();
			loaded_scripts.clear();
			script_allocator.clear();
			clear_devmap();

			custom_script_code_cursor = 0;
		}

		bool read_raw_script_file(const std::string& name, std::string* data)
		{
			if (filesystem::read_file(name, data))
			{
				return true;
			}

			// This will prevent 'fake' GSC raw files from being compiled.
			// They are parsed by the game's own parser later as they are special files.
			if (name.starts_with("maps/createfx") || name.starts_with("maps/createart") ||
				(name.starts_with("maps/mp") && name.ends_with("_fx.gsc")))
			{
				console::debug("Refusing to compile rawfile '%s\n", name.data());
				return false;
			}

			const auto* name_str = name.data();
			if (game::DB_XAssetExists(game::ASSET_TYPE_RAWFILE, name_str) &&
				!game::DB_IsXAssetDefault(game::ASSET_TYPE_RAWFILE, name_str))
			{
				const auto asset = game::DB_FindXAssetHeader(game::ASSET_TYPE_RAWFILE, name.data(), false);
				const auto len = game::DB_GetRawFileLen(asset.rawfile);
				data->resize(len);
				game::DB_GetRawBuffer(asset.rawfile, data->data(), len);
				if (len > 0)
				{
					data->pop_back();
				}

				return true;
			}

			return false;
		}

		std::uint8_t* allocate_custom_script_code(std::size_t size, std::size_t alignment)
		{
			std::uint32_t script_memory_size = 0;
			auto* base = reinterpret_cast<std::uint8_t*>(game::PMem_GetScriptMemory(&script_memory_size));

			const auto base_addr = reinterpret_cast<std::uintptr_t>(base);
			const auto end_addr = base_addr + script_memory_size;

			if (!custom_script_code_cursor)
			{
				custom_script_code_cursor = end_addr;
			}

			custom_script_code_cursor -= size;
			custom_script_code_cursor &= ~(alignment - 1);

			if (custom_script_code_cursor < base_addr)
			{
				throw std::runtime_error(std::format("Out of custom script memory while allocating {} bytes", size));
			}

			const auto page_start = custom_script_code_cursor & ~std::uintptr_t(0xFFF);
			const auto page_end = (custom_script_code_cursor + size + 0xFFF) & ~std::uintptr_t(0xFFF);

			VirtualAlloc(
				reinterpret_cast<void*>(page_start),
				page_end - page_start,
				MEM_COMMIT,
				PAGE_READWRITE
			);

			return reinterpret_cast<std::uint8_t*>(custom_script_code_cursor);
		}

		game::ScriptFile* load_custom_script(const char* file_name, const std::string& real_name)
		{
			if (const auto itr = loaded_scripts.find(real_name); itr != loaded_scripts.end())
			{
				return itr->second;
			}

			try
			{
				auto& compiler = gsc_ctx->compiler();
				auto& assembler = gsc_ctx->assembler();

				std::string source_buffer{};
				if (!read_raw_script_file(real_name + ".gsc", &source_buffer))
				{
					return nullptr;
				}

				std::vector<std::uint8_t> data;
				data.assign(source_buffer.begin(), source_buffer.end());

				const auto assembly_ptr = compiler.compile(real_name, data);
				// Pair of two buffers. First is the byte code and second is the stack
				const auto output_script = assembler.assemble(*assembly_ptr);

				const auto script_file_ptr = static_cast<game::ScriptFile*>(script_allocator.allocate(sizeof(game::ScriptFile)));
				script_file_ptr->name = file_name;

				script_file_ptr->bytecodeLen = static_cast<int>(std::get<0>(output_script).size);
				script_file_ptr->len = static_cast<int>(std::get<1>(output_script).size);

				const auto byte_code_size = static_cast<std::uint32_t>(std::get<0>(output_script).size + 1);
				const auto stack_size = static_cast<std::uint32_t>(std::get<1>(output_script).size + 1);

				script_file_ptr->buffer = static_cast<char*>(script_allocator.allocate(stack_size));
				std::memcpy(const_cast<char*>(script_file_ptr->buffer), std::get<1>(output_script).data, std::get<1>(output_script).size);

				const auto& bytecode = std::get<0>(output_script);
				script_file_ptr->bytecode = allocate_custom_script_code(bytecode.size + 1, 4);

				std::memcpy(script_file_ptr->bytecode, bytecode.data, bytecode.size);
				script_file_ptr->bytecode[bytecode.size] = 0;
				script_file_ptr->compressedLen = 0;

				loaded_scripts[real_name] = script_file_ptr;

				const auto& devmap = std::get<2>(output_script);
				if (devmap.size > 0 && (gsc_ctx->build() & xsk::gsc::build::dev_maps) != xsk::gsc::build::prod)
				{
					add_devmap_entry(script_file_ptr->bytecode, byte_code_size, real_name, devmap);
				}

				return script_file_ptr;
			}
			catch (const std::exception& ex)
			{
				console::error("*********** script compile error *************\n");
				console::error("failed to compile '%s':\n %s\n", real_name.data(), ex.what());
				console::error("**********************************************\n");
				return nullptr;
			}
		}

		std::string get_script_file_name(const std::string& name)
		{
			const auto id = gsc_ctx->token_id(name);
			if (!id)
			{
				return name;
			}

			return std::to_string(id);
		}

		std::pair<xsk::gsc::buffer, std::vector<std::uint8_t>> read_compiled_script_file(const std::string& name, const std::string& real_name)
		{
			const auto* script_file = game::DB_FindXAssetHeader(game::ASSET_TYPE_SCRIPTFILE, name.data(), false).scriptfile;
			if (!script_file)
			{
				throw std::runtime_error(std::format("Could not load scriptfile '{}'", real_name));
			}

			console::info("Decompiling scriptfile '%s'\n", real_name.data());

			const auto len = script_file->compressedLen;
			const std::string stack{script_file->buffer, static_cast<std::uint32_t>(len)};

			const auto decompressed_stack = utils::compression::zlib::decompress(stack);

			std::vector<std::uint8_t> stack_data;
			stack_data.assign(decompressed_stack.begin(), decompressed_stack.end());

			return {{script_file->bytecode, static_cast<std::uint32_t>(script_file->bytecodeLen)}, stack_data};
		}

		void load_script(const std::string& name)
		{
			if (!game::Scr_LoadScript(name.data()))
			{
				return;
			}

			const auto main_handle = game::Scr_GetFunctionHandle(name.data(), gsc_ctx->token_id("main"));
			const auto init_handle = game::Scr_GetFunctionHandle(name.data(), gsc_ctx->token_id("init"));

			if (main_handle)
			{
				console::debug("Loaded '%s::main'\n", name.data());
				main_handles[name] = main_handle;
			}

			if (init_handle)
			{
				console::debug("Loaded '%s::init'\n", name.data());
				init_handles[name] = init_handle;
			}
		}

		void load_scripts_from_folder(const std::filesystem::path& root_dir, const std::filesystem::path& script_dir)
		{
			console::info("Scanning directory '%s' for custom GSC scripts...\n", script_dir.generic_string().data());

			const auto scripts = utils::io::list_files(script_dir.generic_string());
			for (const auto& script : scripts)
			{
				if (!script.string().ends_with(".gsc"))
				{
					continue;
				}

				std::filesystem::path path(script);
				const auto relative = path.lexically_relative(root_dir).generic_string();
				const auto base_name = relative.substr(0, relative.size() - 4);

				load_script(base_name);
			}
		}

		void load_scripts(const std::filesystem::path& root_dir)
		{
			const auto load = [&root_dir](const std::filesystem::path& folder) -> void
			{
				const std::filesystem::path script_dir = root_dir / folder;
				if (utils::io::directory_exists(script_dir.generic_string()))
				{
					load_scripts_from_folder(root_dir, script_dir);
				}
			};

			const std::filesystem::path base_dir = "scripts";

			load(base_dir);

			const auto* map_name = game::Dvar_FindMalleableVar("mapname");

			if (game::environment::is_sp())
			{
				const std::filesystem::path game_folder = "sp";

				load(base_dir / game_folder);

				load(base_dir / game_folder / map_name->current.string);
			}
			else
			{
				const std::filesystem::path game_folder = "mp";

				load(base_dir / game_folder);

				load(base_dir / game_folder / map_name->current.string);

				const auto* game_type = game::Dvar_FindMalleableVar("g_gametype");
				load(base_dir / game_folder / game_type->current.string);
			}
		}

		int db_is_x_asset_default(game::XAssetType type, const char* name)
		{
			if (loaded_scripts.contains(name))
			{
				return 0;
			}

			return game::DB_IsXAssetDefault(type, name);
		}

		void gscr_post_load_scripts_stub()
		{
			utils::hook::invoke<void>(0x59C720_g);

			if (game::virtual_lobby_loaded())
			{
				return;
			}

			for (const auto& path : filesystem::get_search_paths())
			{
				load_scripts(path);
			}
		}

		void db_get_raw_buffer_stub(const game::RawFile* rawfile, char* buf, const int size)
		{
			if (rawfile->len > 0 && rawfile->compressedLen == 0)
			{
				std::memset(buf, 0, size);
				std::memcpy(buf, rawfile->buffer, std::min(rawfile->len, size));
				return;
			}

			game::DB_GetRawBuffer(rawfile, buf, size);
		}

		void g_load_structs_stub()
		{
			if (!game::virtual_lobby_loaded())
			{
				for (auto& function_handle : main_handles)
				{
					console::info("Executing '%s::main'\n", function_handle.first.data());
					const auto thread = game::Scr_ExecThread(static_cast<int>(function_handle.second), 0);
					game::RemoveRefToObject(thread);
				}
			}

			utils::hook::invoke<void>(0x5B79C0_g);
		}

		void g_scr_set_level_script_stub(game::ScriptFunctions* functions)
		{
			if (!game::virtual_lobby_loaded())
			{
				for (const auto& path : filesystem::get_search_paths())
				{
					load_scripts(path);
				}
			}
			
			utils::hook::invoke<void>(0x3BDD70_g, functions);
		}

		void scr_load_level_singleplayer_stub()
		{
			const auto in_virtual_lobby = game::virtual_lobby_loaded();

			if (!in_virtual_lobby)
			{
				for (auto& function_handle : main_handles)
				{
					console::info("Executing '%s::main'\n", function_handle.first.data());
					const auto thread = game::Scr_ExecThread(static_cast<int>(function_handle.second), 0);
					game::RemoveRefToObject(thread);
				}
			}

			utils::hook::invoke<void>(0x3B2250_g);

			if (in_virtual_lobby)
			{
				for (auto& function_handle : init_handles)
				{
					console::info("Executing '%s::init'\n", function_handle.first.data());
					const auto thread = game::Scr_ExecThread(static_cast<int>(function_handle.second), 0);
					game::RemoveRefToObject(thread);
				}
			}
		}

		void scr_load_level_multiplayer_stub()
		{
			utils::hook::invoke<void>(0x5AF770_g);

			if (game::virtual_lobby_loaded())
			{
				return;
			}

			for (auto& function_handle : init_handles)
			{
				console::info("Executing '%s::init'\n", function_handle.first.data());
				const auto thread = game::Scr_ExecThread(static_cast<int>(function_handle.second), 0);
				game::RemoveRefToObject(thread);
			}
		}

		void scr_begin_load_scripts_stub()
		{
			auto build = xsk::gsc::build::prod;

			if (dvars::com_developer && dvars::com_developer->current.integer > 0)
			{
				build = static_cast<xsk::gsc::build>(static_cast<unsigned int>(build) | static_cast<unsigned int>(xsk::gsc::build::dev_maps));
			}

			if (dvars::com_developer_script && dvars::com_developer_script->current.integer > 0)
			{
				build = static_cast<xsk::gsc::build>(static_cast<unsigned int>(build) | static_cast<unsigned int>(xsk::gsc::build::dev_blocks));
			}

			gsc_ctx->init(build, []([[maybe_unused]] const auto* ctx, const auto& included_path) -> std::pair<xsk::gsc::buffer, std::vector<std::uint8_t>>
			{
				const auto script_name = std::filesystem::path(included_path).replace_extension().string();

				std::string file_buffer;
				if (!read_raw_script_file(included_path, &file_buffer) || file_buffer.empty())
				{
					const auto name = get_script_file_name(script_name);
					if (game::DB_XAssetExists(game::ASSET_TYPE_SCRIPTFILE, name.data()))
					{
						return read_compiled_script_file(name, script_name);
					}

					throw std::runtime_error(std::format("Could not load gsc file '{}'", script_name));
				}

				std::vector<std::uint8_t> script_data;
				script_data.assign(file_buffer.begin(), file_buffer.end());

				return {{}, script_data};
			});

			utils::hook::invoke<void>(game::select(0x6856D0, 0x48BED0));
		}

		void scr_end_load_scripts_stub()
		{
			// Cleanup the compiler
			gsc_ctx->cleanup();

			utils::hook::invoke<void>(game::select(0x685800, 0x48C5C0));
		}
	}

	game::ScriptFile* find_script(game::XAssetType type, const char* name, int allow_create_default)
	{
		std::string real_name = name;
		const auto id = static_cast<std::uint16_t>(std::strtol(name, nullptr, 10));
		if (id)
		{
			real_name = gsc_ctx->token_name(id);
		}

		auto* script = load_custom_script(name, real_name);
		if (script)
		{
			return script;
		}

		return game::DB_FindXAssetHeader(type, name, allow_create_default).scriptfile;
	}

	class loading final : public generic_component
	{
	public:
		void post_load() override
		{
			gsc_ctx = std::make_unique<xsk::gsc::s2::context>(xsk::gsc::instance::server);
		}

		void post_unpack() override
		{
			// Increase script mem size 
			// Probably not needed but leaving it here in case we need it (address is for MP).
			// utils::hook::set<std::uint64_t>(0xB76660_g, 0x800000ull);

			// Load our scripts with an uncompressed stack
			utils::hook::call(game::select(0x68F22C, 0x49594C), db_get_raw_buffer_stub);

			utils::hook::call(game::select(0x5AE180, 0x38D665), scr_begin_load_scripts_stub); // GScr_LoadScripts
			utils::hook::call(game::select(0x5AE5B3, 0x38D7B1), scr_end_load_scripts_stub); // GScr_LoadScripts

			// ProcessScript
			utils::hook::call(game::select(0x68F1C7, 0x4958E7), find_script);
			utils::hook::call(game::select(0x68F1D7, 0x4958F7), db_is_x_asset_default);

			// Enable development options
			dvars::com_developer = game::Dvar_RegisterInt("developer", 0, 0, 2, game::DVAR_FLAG_NONE);
			
			// Enable developer script comments: 0 disabled, 1 full developer script, 2 only dev scripts required by art/lighting tweaks.
			// gsc-tool will only have one mode which supports both 1 and 2 dev blocks in the code simultaneously.
			dvars::com_developer_script = game::Dvar_RegisterInt("developer_script", 0, 0, 2, game::DVAR_FLAG_NONE);

			if (game::environment::is_sp())
			{
				utils::hook::call(0x3BDD2F_g, g_scr_set_level_script_stub);
				utils::hook::call(0x373C57_g, scr_load_level_singleplayer_stub);
			}
			else
			{
				// GScr_LoadScripts
				utils::hook::call(0x5AE5AE_g, gscr_post_load_scripts_stub);

				// Exec script handles
				utils::hook::call(0x56084A_g, g_load_structs_stub);
				utils::hook::call(0x560861_g, scr_load_level_multiplayer_stub);
			}

			scripting::on_shutdown([](const int clear_scripts) -> void
			{
				if (clear_scripts)
				{
					clear();
				}
			});
		}
	};
}

REGISTER_COMPONENT(gsc::loading)
