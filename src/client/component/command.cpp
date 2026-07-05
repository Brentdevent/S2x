#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console/console.hpp"

#include <game/game.hpp>

#include <utils/hook.hpp>
#include <utils/string.hpp>
#include <utils/memory.hpp>
#include <utils/io.hpp>

namespace command
{
	namespace
	{
		utils::hook::detour client_command_mp_hook;
		utils::hook::detour client_command_sp_hook;

		std::unordered_map<std::string, command_param_function>& get_command_map()
		{
			static std::unordered_map<std::string, command_param_function> map{};
			return map;
		}

		std::unordered_map<std::string, sv_command_param_function>& get_sv_command_map()
		{
			static std::unordered_map<std::string, sv_command_param_function> map{};
			return map;
		}

		void execute_custom_command()
		{
			const params params{};
			const auto command = utils::string::to_lower(params[0]);

			auto& map = get_command_map();
			const auto entry = map.find(command);

			if (entry != map.end())
			{
				entry->second(params);
			}
		}

		bool execute_custom_sv_command_internal(const int client_num, const params_sv& params)
		{
			const auto command = utils::string::to_lower(params[0]);

			auto& map = get_sv_command_map();
			const auto entry = map.find(command);

			if (entry == map.end())
			{
				return false;
			}

			entry->second(client_num, params);
			return true;
		}

		void execute_custom_sv_command()
		{
			const params_sv params{};

			execute_custom_sv_command_internal(-1, params);
		}

		void client_command_mp_stub(const int client_num)
		{
			const params_sv params{};

			const auto handled = execute_custom_sv_command_internal(client_num, params);
			if (!handled)
			{
				client_command_mp_hook.invoke<void>(client_num);
			}
		}

		void client_command_sp_stub(const int client_num, const char* text)
		{
			params_sv params{ text };

			const auto handled = execute_custom_sv_command_internal(client_num, params);
			if (!handled)
			{
				client_command_sp_hook.invoke<void>(client_num, text);
			}
		}

		void add_raw(const char* name, void(*callback)())
		{
			auto& allocator = *utils::memory::get_allocator();

			const auto* command_name = allocator.duplicate_string(name);
			auto* cmd_function = allocator.allocate<game::cmd_function_s>();

			game::Cmd_AddCommandInternal(command_name, callback, cmd_function);
		}

		void add_server_raw(const char* name)
		{
			auto& allocator = *utils::memory::get_allocator();

			const auto* command_name = allocator.duplicate_string(name);
			auto* cmd_function = allocator.allocate<game::cmd_function_s>();

			game::Cmd_AddCommandInternal(command_name, game::Cbuf_AddServerText_f, cmd_function);
			game::Cmd_AddServerCommandInternal(command_name, execute_custom_sv_command, cmd_function);
		}
	}

	params::params()
		: nesting_(game::cmd_args->nesting)
	{
		assert(this->nesting_ < game::CMD_MAX_NESTING);
	}

	params::params(const std::string& text)
		: needs_end_(true)
	{
		game::Cmd_TokenizeString(text.data());
		this->nesting_ = game::cmd_args->nesting;

		assert(this->nesting_ < game::CMD_MAX_NESTING);
	}

	params::~params()
	{
		if (this->needs_end_)
		{
			game::Cmd_EndTokenizedString();
		}
	}

	int params::size() const
	{
		return game::cmd_args->argc[this->nesting_];
	}

	const char* params::get(const int index) const
	{
		if (index < 0 || index >= this->size())
		{
			return "";
		}

		return game::cmd_args->argv[this->nesting_][index];
	}

	std::string params::join(const int index) const
	{
		std::string result{};

		for (auto i = index; i < this->size(); ++i)
		{
			if (i > index)
			{
				result.append(" ");
			}

			result.append(this->get(i));
		}

		return result;
	}

	std::vector<std::string> params::get_all() const
	{
		std::vector<std::string> result{};

		for (auto i = 0; i < this->size(); ++i)
		{
			result.emplace_back(this->get(i));
		}

		return result;
	}

	params_sv::params_sv()
		: nesting_(game::sv_cmd_args->nesting)
	{
		assert(this->nesting_ < game::CMD_MAX_NESTING);
	}

	params_sv::params_sv(const std::string& text)
		: needs_end_(true)
	{
		game::SV_Cmd_TokenizeString(text.data());
		this->nesting_ = game::sv_cmd_args->nesting;

		assert(this->nesting_ < game::CMD_MAX_NESTING);
	}

	params_sv::~params_sv()
	{
		if (this->needs_end_)
		{
			game::SV_Cmd_EndTokenizedString();
		}
	}

	int params_sv::size() const
	{
		return game::sv_cmd_args->argc[this->nesting_];
	}

	const char* params_sv::get(const int index) const
	{
		if (index < 0 || index >= this->size())
		{
			return "";
		}

		return game::sv_cmd_args->argv[this->nesting_][index];
	}

	std::string params_sv::join(const int index) const
	{
		std::string result{};

		for (auto i = index; i < this->size(); ++i)
		{
			if (i > index)
			{
				result.append(" ");
			}

			result.append(this->get(i));
		}

		return result;
	}

	std::vector<std::string> params_sv::get_all() const
	{
		std::vector<std::string> result{};

		for (auto i = 0; i < this->size(); ++i)
		{
			result.emplace_back(this->get(i));
		}

		return result;
	}

	void add(const std::string& command, command_function function)
	{
		add(command, [function = std::move(function)](const params&)
		{
			function();
		});
	}

	void add(const std::string& command, command_param_function function)
	{
		const auto lower_command = utils::string::to_lower(command);

		auto& map = get_command_map();
		const auto already_registered = map.contains(lower_command);

		map[lower_command] = std::move(function);

		if (already_registered)
		{
			return;
		}

		add_raw(command.data(), execute_custom_command);
	}

	void add_sv(const std::string& command, sv_command_param_function function)
	{
		const auto lower_command = utils::string::to_lower(command);

		auto& map = get_sv_command_map();
		const auto already_registered = map.contains(lower_command);

		map[lower_command] = std::move(function);

		if (already_registered)
		{
			return;
		}

		add_server_raw(command.data());
	}

	struct component final : generic_component
	{
		void post_unpack() override
		{
			if (game::environment::is_mp())
			{
				client_command_mp_hook.create(0x54EE80_g, client_command_mp_stub);
			}
			else
			{
				client_command_sp_hook.create(0x366270_g, client_command_sp_stub);
			}

			command::add("quit", []()
			{
				game::Com_Quit_f();
			});
		}
	};
}

REGISTER_COMPONENT(command::component)
