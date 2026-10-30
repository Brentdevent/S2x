#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "mode_switch.hpp"
#include "game/game.hpp"
#include "game/mode_switch_arguments.hpp"
#include "game/player_profile.hpp"

#include <utils/finally.hpp>
#include <utils/flags.hpp>
#include <utils/hook.hpp>
#include <utils/nt.hpp>
#include <utils/string.hpp>

#include <charconv>
#include <system_error>

namespace mode_switch
{
	namespace
	{
		std::vector<std::wstring> saved_arguments;
		std::wstring working_directory;

		[[noreturn]] void throw_last_error(const char* operation)
		{
			throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
		}

		std::vector<std::wstring> parse_arguments(const std::wstring& command_line)
		{
			int count{};
			auto* argv = CommandLineToArgvW(command_line.c_str(), &count);
			if (!argv)
			{
				throw_last_error("CommandLineToArgvW");
			}

			const auto cleanup = utils::finally([argv] { LocalFree(argv); });
			return {argv + 1, argv + count};
		}

		game::environment::mode get_target_mode(const char* executable, const std::vector<std::wstring>& args)
		{
			if (executable && !_stricmp(executable, "s2_sp64_ship.exe"))
			{
				return game::environment::mode::singleplayer;
			}

			if (!executable || _stricmp(executable, "s2_mp64_ship.exe"))
			{
				throw std::runtime_error("Unsupported game executable in mode switch.");
			}

			for (size_t i = 0; i + 1 < args.size(); ++i)
			{
				const auto option = arguments::lower(args[i]);
				if ((option == L"+zombiesmode" || option == L"+enterlocalzombies") && args[i + 1] == L"1")
				{
					return game::environment::mode::zombies;
				}
			}

			return game::environment::mode::multiplayer;
		}

		void start_process(std::vector<std::wstring> args)
		{
			FILETIME creation{}, exit{}, kernel{}, user{};
			if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
			{
				throw_last_error("GetProcessTimes");
			}

			const auto creation_time = (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32)
				| creation.dwLowDateTime;
			args.emplace_back(L"-wait-for-parent");
			args.push_back(std::to_wstring(GetCurrentProcessId()) + L":" + std::to_wstring(creation_time));

			const auto executable = utils::nt::library::get_by_address(start_process).get_path().wstring();
			auto command_line = arguments::quote(executable);
			for (const auto& arg : args)
			{
				command_line += L" " + arguments::quote(arg);
			}

			STARTUPINFOW startup{};
			startup.cb = sizeof(startup);
			PROCESS_INFORMATION child{};
			if (!CreateProcessW(executable.c_str(), command_line.data(), nullptr, nullptr, FALSE,
				0, nullptr, working_directory.c_str(), &startup, &child))
			{
				throw_last_error("CreateProcessW");
			}

			const utils::nt::handle<> thread{child.hThread};
			const utils::nt::handle<> process{child.hProcess};
			AllowSetForegroundWindow(child.dwProcessId);
		}

		bool launch_game(const char* executable, const char* command_line)
		{
			try
			{
				const auto engine_arguments = parse_arguments(L"s2x " +
					utils::string::convert(std::string{command_line ? command_line : ""}));
				const auto target = get_target_mode(executable, engine_arguments);
				if (!game::environment::is_mode_installed(target))
				{
					throw std::runtime_error("The selected game mode is not installed. Missing executable: " +
						game::environment::get_binary_path(target).string());
				}

				auto args = saved_arguments;
				args.insert(args.end(), engine_arguments.begin(), engine_arguments.end());

				switch (target)
				{
				case game::environment::mode::singleplayer:
					args.emplace_back(L"-singleplayer");
					break;
				case game::environment::mode::multiplayer:
					args.emplace_back(L"-multiplayer");
					break;
				case game::environment::mode::zombies:
					args.emplace_back(L"-zombies");
					break;
				}

				args.emplace_back(L"-profile");
				args.push_back(utils::string::convert(player_profile::name()));
				start_process(std::move(args));
				return true;
			}
			catch (const std::exception& e)
			{
				MessageBoxA(nullptr, e.what(), "S2x Mode Switch Failed", MB_OK | MB_ICONERROR);
				return false;
			}
		}
	}

	void wait_for_previous_process()
	{
		if (!utils::flags::has_flag("-wait-for-parent"))
		{
			return;
		}

		const auto value = utils::flags::get_value("-wait-for-parent").value_or("");
		const auto separator = value.find(':');
		if (separator == std::string::npos)
		{
			throw std::runtime_error("Invalid mode-switch parent process.");
		}

		DWORD parent_id{};
		std::uint64_t expected_creation{};
		const auto [pid_end, pid_error] = std::from_chars(value.data(), value.data() + separator, parent_id);
		const auto [time_end, time_error] = std::from_chars(value.data() + separator + 1,
			value.data() + value.size(), expected_creation);

		if (pid_error != std::errc{} || pid_end != value.data() + separator
			|| time_error != std::errc{} || time_end != value.data() + value.size()
			|| !parent_id || !expected_creation)
		{
			throw std::runtime_error("Invalid mode-switch parent process.");
		}

		const utils::nt::handle<> parent{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, parent_id)};
		if (!parent)
		{
			if (GetLastError() == ERROR_INVALID_PARAMETER) return;
			throw_last_error("OpenProcess");
		}

		FILETIME creation{}, exit{}, kernel{}, user{};
		if (!GetProcessTimes(parent, &creation, &exit, &kernel, &user))
		{
			throw_last_error("GetProcessTimes");
		}

		const auto creation_time = (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32)
			| creation.dwLowDateTime;
		// An updater relaunch may retain an old process ID that Windows has reused.
		if (creation_time != expected_creation)
		{
			return;
		}

		if (parent_id == GetCurrentProcessId())
		{
			throw std::runtime_error("Cannot wait for the current process during a mode switch.");
		}

		// The previous process must release its profile reservation first.
		if (WaitForSingleObject(parent, INFINITE) != WAIT_OBJECT_0)
		{
			throw_last_error("WaitForSingleObject");
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated())
			{
				return;
			}

			saved_arguments = arguments::retain(parse_arguments(GetCommandLineW()));
			working_directory = std::filesystem::current_path().wstring();

			utils::hook::jump(game::select(0x830610, 0x5BBE20), launch_game);
		}
	};
}

REGISTER_COMPONENT(mode_switch::component)
