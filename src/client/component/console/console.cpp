#include <std_include.hpp>
#include "console.hpp"

#include "game/game.hpp"

#include "component/scheduler.hpp"
#include "component/server_commands.hpp"

#include <utils/flags.hpp>

#include "terminal.hpp"
#include "syscon.hpp"

#include <utils/io.hpp>

namespace game_console
{
	void print(int type, const std::string& data);
}

namespace console
{
	namespace
	{
		constexpr std::size_t console_format_buffer_size = 0x1000;
		constexpr std::size_t maximum_console_message_size = 1024 * 1024;
		constexpr std::string_view console_truncation_marker =
			"\n[console output truncated]\n";
		std::atomic<std::shared_ptr<const std::string>> log_path;
	}

	enum console_type
	{
		con_type_none,
		con_type_terminal,
		con_type_syscon,
		con_type_game = con_type_syscon,
		con_type_default = con_type_syscon,
	} con_type;

	game::dvar_t* console_log = nullptr;

	void init_console_type()
	{
		con_type = con_type_default;

		const auto noconsole_flag = utils::flags::has_flag("-noconsole");
		if (!game::environment::is_dedicated() && noconsole_flag)
		{
			con_type = con_type_none;
			return;
		}

		const auto terminal_flag = utils::flags::has_flag("-terminal");
		if (terminal_flag)
		{
			con_type = con_type_terminal;
			return;
		}

		const auto syscon_flag = utils::flags::has_flag("-syscon");
		if (syscon_flag)
		{
			con_type = con_type_syscon;
			return;
		}
	}

	auto get_console_type()
	{
		return con_type;
	}

	bool is_enabled()
	{
		return get_console_type() != console_type::con_type_none;
	}

	namespace terminal
	{
		bool is_enabled()
		{
			return get_console_type() == console_type::con_type_terminal;
		}
	}

	namespace syscon
	{
		bool is_enabled()
		{
			return get_console_type() == console_type::con_type_syscon;
		}
	}

	std::string format(va_list* ap, const char* message)
	{
		if (!ap || !message)
		{
			return {};
		}

		static thread_local char buffer[console_format_buffer_size];
		va_list arguments;
		va_copy(arguments, *ap);
		const auto count = _vsnprintf_s(
			buffer, sizeof(buffer), _TRUNCATE, message, arguments);
		va_end(arguments);

		if (count >= 0)
		{
			return {buffer, static_cast<std::size_t>(count)};
		}

		va_copy(arguments, *ap);
		const auto required_count = _vscprintf(message, arguments);
		va_end(arguments);
		if (required_count < 0)
		{
			return {};
		}

		const auto required_size = static_cast<std::size_t>(required_count);
		const auto truncated = required_size > maximum_console_message_size;
		const auto output_capacity = truncated
			? maximum_console_message_size - console_truncation_marker.size()
			: required_size;
		std::vector<char> dynamic_buffer(output_capacity + 1);

		va_copy(arguments, *ap);
		const auto dynamic_count = _vsnprintf_s(dynamic_buffer.data(), dynamic_buffer.size(),
			_TRUNCATE, message, arguments);
		va_end(arguments);

		if (!truncated && dynamic_count < 0)
		{
			return {};
		}

		const auto written = truncated
			? strnlen_s(dynamic_buffer.data(), dynamic_buffer.size())
			: static_cast<std::size_t>(dynamic_count);
		std::string result{dynamic_buffer.data(), written};
		if (truncated)
		{
			result.append(console_truncation_marker);
		}

		return result;
	}

	void dispatch_message(const int type, const std::string& message)
	{
		if (server_commands::message_redirect(message))
		{
			return;
		}

		std::string out = message;
		if (out.empty() || out.back() != '\n')
		{
			out.push_back('\n');
		}

		// Logs also originate on the Demonware worker. Never borrow the Dvar's
		// live string there; its main-thread producer owns this immutable copy.
		const auto path = log_path.load(std::memory_order_acquire);
		if (path && !path->empty())
			utils::io::write_file(*path, out, true);

		if (console::is_enabled())
		{
			if (terminal::is_enabled())
			{
				::terminal::dispatch_message(type, message);
				return;
			}
			else if (syscon::is_enabled())
			{
				::syscon::Sys_Print(message.data());
			}
		}

		game_console::print(type, message);
	}

	void print(const int type, const char* fmt, ...)
	{
		if (type == console::print_type_demonware)
		{
			static bool has_demonware_debug = utils::flags::has_flag("-demonware_debug");
			if (!has_demonware_debug)
			{
				return;
			}
		}

		va_list ap;
		va_start(ap, fmt);
		const auto result = format(&ap, fmt);
		va_end(ap);

		dispatch_message(type, result);
	}

	void set_title(const std::string& title)
	{
		if (console::is_enabled())
		{
			if (terminal::is_enabled())
			{
				SetConsoleTitleA(title.data());
			}
			else if (syscon::is_enabled())
			{
				::syscon::set_title(title);
			}
		}
	}

	void init()
	{
		static auto initialized = false;
		if (initialized) return;
		initialized = true;

		init_console_type();
		if (get_console_type() == con_type_none)
		{
			return;
		}
		else if (get_console_type() == con_type_terminal)
		{
			::terminal::init();
		}
		else if (get_console_type() == con_type_syscon)
		{
			::syscon::init();
		}

		scheduler::once([]()
		{
			console_log = game::Dvar_RegisterString("g_consoleLog", "s2x/logs/console.log", game::DVAR_FLAG_SAVED);
			scheduler::loop([]
			{

				const auto text = console_log ? console_log->current.string : nullptr;
				const std::string value = text ? text : "";
				const auto previous = log_path.load(std::memory_order_acquire);
				if (!previous || *previous != value)
					log_path.store(std::make_shared<const std::string>(value), std::memory_order_release);
			}, scheduler::main);
		}, scheduler::main);
	}
}
