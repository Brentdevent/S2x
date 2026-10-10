#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"

#include "game/scripting/functions.hpp"

#include "scheduler.hpp"
#include "scripting.hpp"

#include "gsc/script_loading.hpp"

#include <utils/hook.hpp>

namespace scripting
{
	std::unordered_map<std::string, std::unordered_map<std::string, const char*>> script_function_table;
	std::unordered_map<std::string, std::vector<std::pair<std::string, const char*>>> script_function_table_sort;
	std::unordered_map<const char*, std::pair<std::string, std::string>> script_function_table_rev;

	std::string current_file;

	namespace
	{
		utils::hook::detour vm_notify_hook;
		utils::hook::detour scr_load_level_hook;
		utils::hook::detour g_shutdown_game_hook;
		utils::hook::detour scr_free_scripts_hook;

		utils::hook::detour scr_set_thread_position_hook;
		utils::hook::detour process_script_hook;
		utils::hook::detour sl_get_canonical_string_hook;

		std::string current_script_file;
		std::uint32_t current_file_id = 0;

		std::unordered_map<unsigned int, std::string> canonical_string_table;

		std::vector<std::function<void(int)>> shutdown_callbacks;
		std::vector<std::function<void()>> scripts_free_callbacks;
		std::vector<std::function<void()>> init_callbacks;

		void scr_load_level_stub()
		{
			scr_load_level_hook.invoke<void>();

			if (!game::virtual_lobby_loaded())
			{
				for (const auto& callback : init_callbacks)
				{
					callback();
				}
			}
		}

		void g_shutdown_game_stub(const int clear_scripts)
		{
			for (const auto& callback : shutdown_callbacks)
			{
				callback(clear_scripts);
			}

			g_shutdown_game_hook.invoke<void>(clear_scripts);
		}

		void scr_free_scripts_stub()
		{
			// Keep script memory alive until the engine releases its references.
			scr_free_scripts_hook.invoke<void>();
			script_function_table_sort.clear();
			script_function_table.clear();
			script_function_table_rev.clear();
			canonical_string_table.clear();

			for (const auto& callback : scripts_free_callbacks)
			{
				callback();
			}
		}

		void process_script_stub(const char* filename)
		{
			current_script_file = filename;

			const auto file_id = atoi(filename);
			if (file_id)
			{
				current_file_id = static_cast<std::uint16_t>(file_id);
			}
			else
			{
				current_file_id = 0;
				current_file = filename;
			}

			process_script_hook.invoke<void>(filename);
		}

		void add_function_sort(unsigned int id, const char* pos)
		{
			std::string filename = current_file;
			if (current_file_id)
			{
				filename = get_token(current_file_id);
			}

			if (!script_function_table_sort.contains(filename))
			{
				const auto script = gsc::find_script(game::ASSET_TYPE_SCRIPTFILE, current_script_file.data(), false);
				if (script)
				{
					const auto* end = &script->bytecode[script->bytecodeLen];
					script_function_table_sort[filename].emplace_back("__end__", reinterpret_cast<const char*>(end));
				}
			}

			const auto name = scripting::get_token(id);
			auto& itr = script_function_table_sort[filename];
			itr.insert(itr.end() - 1, {name, pos});
		}

		void add_function(const std::string& file, unsigned int id, const char* pos)
		{
			const auto name = get_token(id);
			script_function_table[file][name] = pos;
			script_function_table_rev[pos] = {file, name};
		}

		void scr_set_thread_position_stub(unsigned int thread_name, const char* code_pos)
		{
			add_function_sort(thread_name, code_pos);

			if (current_file_id)
			{
				const auto name = get_token(current_file_id);
				add_function(name, thread_name, code_pos);
			}
			else
			{
				add_function(current_file, thread_name, code_pos);
			}

			scr_set_thread_position_hook.invoke<void>(thread_name, code_pos);
		}

		unsigned int sl_get_canonical_string_stub(const char* str)
		{
			const auto result = sl_get_canonical_string_hook.invoke<unsigned int>(str);
			canonical_string_table[result] = str;
			return result;
		}
	}

	std::string get_token(unsigned int id)
	{
		if (const auto itr = canonical_string_table.find(id); itr != canonical_string_table.end())
		{
			return itr->second;
		}

		return find_token(id);
	}

	void on_shutdown(const std::function<void(int)>& callback)
	{
		shutdown_callbacks.push_back(callback);
	}

	void on_init(const std::function<void()>& callback)
	{
		init_callbacks.push_back(callback);
	}

	void on_scripts_free(const std::function<void()>& callback)
	{
		scripts_free_callbacks.push_back(callback);
	}

	std::optional<std::string> get_canonical_string(const unsigned int id)
	{
		if (const auto itr = canonical_string_table.find(id); itr != canonical_string_table.end())
		{
			return {itr->second};
		}

		return {};
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			scr_load_level_hook.create(game::Scr_LoadLevel, &scr_load_level_stub);
			g_shutdown_game_hook.create(game::G_ShutdownGame, &g_shutdown_game_stub);
			scr_free_scripts_hook.create(game::select(0x685890, 0x48C090), &scr_free_scripts_stub);

			scr_set_thread_position_hook.create(game::Scr_SetThreadPosition, &scr_set_thread_position_stub);
			process_script_hook.create(game::ProcessScript, &process_script_stub);
			sl_get_canonical_string_hook.create(game::SL_GetCanonicalString, &sl_get_canonical_string_stub);
		}
	};
}

REGISTER_COMPONENT(scripting::component)
