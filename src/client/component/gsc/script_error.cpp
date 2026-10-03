#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"

#include "script_extension.hpp"
#include "script_error.hpp"

#include "component/scripting.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

using namespace utils::string;

namespace gsc
{
	namespace
	{
		utils::hook::detour scr_emit_function_hook;

		unsigned int current_filename = 0;

		std::string unknown_function_error;

		void scr_emit_function_stub(unsigned int filename, unsigned int thread_name, char* code_pos)
		{
			current_filename = filename;
			scr_emit_function_hook.invoke<void>(filename, thread_name, code_pos);
		}

		std::string get_filename_name()
		{
			const auto filename_str = game::SL_ConvertToString(static_cast<game::scr_string_t>(current_filename));
			const auto id = std::atoi(filename_str);
			if (!id)
			{
				return filename_str;
			}

			return scripting::get_token(id);
		}

		void get_unknown_function_error(const char* code_pos)
		{
			const auto function = find_function(code_pos);
			if (function.has_value())
			{
				const auto& pos = function.value();
				unknown_function_error = std::format(
					"while processing function '{}' in script '{}':\nunknown script '{}'", pos.first, pos.second, scripting::current_file
				);
			}
			else
			{
				unknown_function_error = std::format("unknown script '{}'", scripting::current_file);
			}
		}

		void get_unknown_function_error(unsigned int thread_name)
		{
			const auto filename = get_filename_name();
			const auto name = scripting::get_token(thread_name);

			unknown_function_error = std::format(
				"while processing script '{}':\nunknown function '{}::{}'", scripting::current_file, filename, name
			);
		}

		void compile_error_stub(const char* code_pos, [[maybe_unused]] const char* msg)
		{
			get_unknown_function_error(code_pos);
			game::Com_Error(game::ERR_DROP, "script link error\n%s", unknown_function_error.data());
		}

		unsigned int find_variable_stub(unsigned int parent_id, unsigned int thread_name)
		{
			const auto res = game::FindVariable(parent_id, thread_name);
			if (!res)
			{
				get_unknown_function_error(thread_name);
				game::Com_Error(game::ERR_DROP, "script link error\n%s", unknown_function_error.data());
			}

			return res;
		}
	}

	std::optional<std::pair<std::string, std::string>> find_function(const char* pos)
	{
		for (const auto& file : scripting::script_function_table_sort)
		{
			for (auto i = file.second.begin(); i != file.second.end() && std::next(i) != file.second.end(); ++i)
			{
				const auto next = std::next(i);
				if (pos >= i->second && pos < next->second)
				{
					return {std::make_pair(i->first, file.first)};
				}
			}
		}

		return {};
	}

	class error final : public generic_component
	{
	public:
		void post_unpack() override
		{
			scr_emit_function_hook.create(game::Scr_EmitFunction, &scr_emit_function_stub);

			utils::hook::call(game::select(0x684DB3, 0x610383, 0x48B5D3), compile_error_stub); // LinkFile
			utils::hook::call(game::select(0x684E89, 0x610459, 0x48B6A9), find_variable_stub); // Scr_EmitFunction
		}

		void pre_destroy() override
		{
			scr_emit_function_hook.clear();
		}
	};
}

REGISTER_COMPONENT(gsc::error)
