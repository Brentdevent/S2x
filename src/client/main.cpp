#include <std_include.hpp>

#include "loader/component_loader.hpp"
#include "loader/loader.hpp"

#include <utils/finally.hpp>
#include <utils/hook.hpp>
#include <utils/nt.hpp>
#include <utils/io.hpp>
#include <utils/flags.hpp>
#include <utils/string.hpp>

#include "game/game.hpp"
#include "launcher/launcher.hpp"
#include "component/console/console.hpp"
#include "component/updater.hpp"

namespace
{
	volatile bool g_call_tls_callbacks = false;

	utils::hook::detour exit_hook;

	void exit_stub(int code)
	{
		component_loader::pre_destroy();
		exit_hook.invoke(code);
	}

	DWORD_PTR WINAPI set_thread_affinity_mask(HANDLE hThread, DWORD_PTR dwThreadAffinityMask)
	{
		component_loader::post_unpack();

		return SetThreadAffinityMask(hThread, dwThreadAffinityMask);
	}

	void patch_imports()
	{
		const utils::nt::library ucrt{ "ucrtbase.dll" };
		auto* exit_func = ucrt.get_proc<void*>("exit");
		exit_hook.create(exit_func, exit_stub);

		const utils::nt::library game{};
		utils::hook::set(game.get_iat_entry("kernel32.dll", "SetThreadAffinityMask"), set_thread_affinity_mask);
	}

	void remove_crash_file()
	{
		utils::io::remove_file("__s2Exe");
	}

	void enable_dpi_awareness()
	{
		const utils::nt::library user32{ "user32.dll" };

		{
			const auto set_dpi = user32
				? user32.get_proc<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>(
					"SetProcessDpiAwarenessContext")
				: nullptr;
			if (set_dpi)
			{
				set_dpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
				return;
			}
		}

		{
			const utils::nt::library shcore{ "shcore.dll" };
			const auto set_dpi = shcore
				? shcore.get_proc<HRESULT(WINAPI*)(PROCESS_DPI_AWARENESS)>(
					"SetProcessDpiAwareness")
				: nullptr;
			if (set_dpi)
			{
				set_dpi(PROCESS_PER_MONITOR_DPI_AWARE);
				return;
			}
		}

		{
			const auto set_dpi = user32
				? user32.get_proc<BOOL(WINAPI*)()>(
					"SetProcessDPIAware")
				: nullptr;
			if (set_dpi)
			{
				set_dpi();
			}
		}
	}

	PIMAGE_TLS_CALLBACK* get_tls_callbacks()
	{
		const utils::nt::library game{};
		const auto& entry = game.get_optional_header()->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
		if (!entry.VirtualAddress || !entry.Size)
		{
			return nullptr;
		}

		const auto* tls_dir = reinterpret_cast<IMAGE_TLS_DIRECTORY*>(game.get_ptr() + entry.VirtualAddress);
		return reinterpret_cast<PIMAGE_TLS_CALLBACK*>(tls_dir->AddressOfCallBacks);
	}

	void run_tls_callbacks(const DWORD reason)
	{
		if (!g_call_tls_callbacks)
		{
			return;
		}

		auto* callback = get_tls_callbacks();
		while (callback && *callback)
		{
			(*callback)(GetModuleHandleA(nullptr), reason, nullptr);
			++callback;
		}
	}

	[[maybe_unused]] thread_local struct tls_runner
	{
		tls_runner()
		{
			run_tls_callbacks(DLL_THREAD_ATTACH);
		}

		~tls_runner()
		{
			run_tls_callbacks(DLL_THREAD_DETACH);
		}
	} tls_runner;

	FARPROC load_process(const std::string& procname)
	{
		const auto proc = loader::load_binary(procname);

		auto* const peb = reinterpret_cast<PPEB>(__readgsqword(0x60));
		peb->Reserved3[1] = proc.get_ptr();
		static_assert(offsetof(PEB, Reserved3[1]) == 0x10);

		return FARPROC(proc.get_ptr() + proc.get_relative_entry_point());
	}

	bool handle_process_runner()
	{
		const auto* const command = "-proc ";
		const char* parent_proc = strstr(GetCommandLineA(), command);

		if (!parent_proc)
		{
			return false;
		}

		const auto pid = DWORD(atoi(parent_proc + strlen(command)));
		const utils::nt::handle<> process_handle = OpenProcess(SYNCHRONIZE, FALSE, pid);
		if (process_handle)
		{
			WaitForSingleObject(process_handle, INFINITE);
		}

		return true;
	}

	bool has_zombies_argument()
	{
		const auto value = utils::flags::get_value("+zombiesMode");
		return value.has_value() && value.value() == "1";
	}

	bool has_dedicated_argument()
	{
		const auto value = utils::flags::get_set_value("dedicated");
		return utils::flags::has_flag("-dedicated") || (value.has_value() && value.value() == "1");
	}

	struct startup_options
	{
		std::optional<game::environment::mode> gameplay_mode{};
		bool dedicated{};
	};

	startup_options detect_startup_options()
	{
		startup_options options{};
		options.dedicated = has_dedicated_argument();

		const auto multiplayer = utils::flags::has_flag("-multiplayer");
		const auto singleplayer = utils::flags::has_flag("-singleplayer");
		const auto zombies = utils::flags::has_flag("-zombies") || has_zombies_argument();

		if (!options.dedicated && multiplayer)
		{
			options.gameplay_mode = game::environment::mode::multiplayer;
		}
		else if (singleplayer)
		{
			options.gameplay_mode = game::environment::mode::singleplayer;
		}
		else if (zombies)
		{
			options.gameplay_mode = game::environment::mode::zombies;
		}
		else if (options.dedicated)
		{
			options.gameplay_mode = game::environment::mode::multiplayer;
		}

		return options;
	}
}

int main()
{
	if (handle_process_runner())
	{
		return 0;
	}

	FARPROC entry_point{};
	srand(uint32_t(time(nullptr)) ^ ~(GetTickCount() * GetCurrentProcessId()));

	{
		auto premature_shutdown = true;
		const auto _ = utils::finally([&premature_shutdown]
		{
			if (premature_shutdown)
			{
				component_loader::pre_destroy();
			}
		});

		try
		{
			const auto application_directory = utils::nt::library{}.get_folder();
			const auto is_microsoft_store_install = utils::io::file_exists(
				(application_directory / "MicrosoftGame.config").wstring());

			game::environment::set_platform(is_microsoft_store_install
				? game::environment::platform::microsoft_store
				: game::environment::platform::steam);

			if (game::environment::is_microsoft_store())
			{
				std::filesystem::current_path(application_directory);
			}

			auto options = detect_startup_options();
			if (options.dedicated
				&& options.gameplay_mode == game::environment::mode::singleplayer)
			{
				throw std::runtime_error("Singleplayer dedicated servers are not supported.");
			}

			if (options.gameplay_mode.has_value())
			{
				game::environment::set_mode(*options.gameplay_mode);
			}

			game::environment::set_dedicated(options.dedicated);

			enable_dpi_awareness();
			remove_crash_file();

			if (!options.gameplay_mode.has_value())
			{
				const launcher launcher;
				options.gameplay_mode = launcher.run();

				if (!options.gameplay_mode.has_value()) return 0;

				game::environment::set_mode(*options.gameplay_mode);
			}

			launcher::apply_saved_launch_options();
			console::init();

			if (game::environment::is_zombies() && !has_zombies_argument())
			{
				utils::nt::relaunch_self("+zombiesMode 1");
				return 0;
			}

			const auto mp_binary = game::environment::is_microsoft_store()
				? "s2x_mp64_ship.exe"s
				: "s2_mp64_ship.exe"s;
			const auto sp_binary = game::environment::is_microsoft_store()
				? "s2x_sp64_ship.exe"s
				: "s2_sp64_ship.exe"s;

			const auto& binary_to_load = game::environment::uses_multiplayer_binary() ? mp_binary : sp_binary;
			auto game_directory = application_directory;
			if (!game::environment::is_microsoft_store()
				&& !utils::io::file_exists((game_directory / binary_to_load).wstring()))
			{
				game_directory = std::filesystem::current_path();
			}

			const auto binary_path = game_directory / binary_to_load;
			const auto updates_disabled = utils::flags::has_flag("-noupdate");
			std::optional<std::string> store_runtime_update_warning{};

			if (game::environment::is_microsoft_store() && !updates_disabled)
			{
				try
				{
					updater::update_store_runtime(game_directory, binary_to_load);
				}
				catch (const std::exception& e)
				{
					if (!utils::io::file_exists(binary_path.wstring()))
					{
						throw std::runtime_error(utils::string::va(
							"S2x could not obtain the required Microsoft Store/Xbox runtime '%s', and no "
							"usable local runtime is installed.\n\n%s\n\n"
							"Do not replace or rename the original Microsoft Store executables.",
							binary_to_load.data(), e.what()
						));
					}

					store_runtime_update_warning = e.what();
				}
			}

			if (!utils::io::file_exists(binary_path.wstring()))
			{
				if (game::environment::is_microsoft_store())
				{
					const auto reason = updates_disabled
						? "Automatic updates are disabled. Relaunch S2x without -noupdate so it can download the required runtime."
						: "S2x could not download the required runtime. Check the update source and installation permissions.";
					throw std::runtime_error(utils::string::va(
						"S2x detected a Microsoft Store/Xbox installation, but '%s' is missing.\n\n%s\n\n"
						"Do not replace or rename the original Microsoft Store executables.",
						binary_to_load.data(), reason
					));
				}

				throw std::runtime_error(utils::string::va(
					"Could not find '%s'.\n\n"
					"Make sure S2x.exe is placed in your Call of Duty: WWII installation folder.",
					binary_to_load.data()
				));
			}

			if (!component_loader::activate(!game::environment::uses_multiplayer_binary()))
			{
				return 1;
			}

			try
			{
				entry_point = load_process(binary_path.generic_string());
			}
			catch (const std::exception& e)
			{
				if (!game::environment::is_microsoft_store())
				{
					throw;
				}

				const auto action = updates_disabled
					? "Relaunch S2x without -noupdate so it can download a valid runtime."
					: "Check your connection and relaunch S2x so it can update the runtime.";
				throw std::runtime_error(utils::string::va(
					"The Microsoft Store/Xbox runtime '%s' could not be loaded.\n\n%s\n\n%s",
					binary_to_load.data(), action, e.what()
				));
			}

			if (!entry_point)
			{
				if (game::environment::is_microsoft_store())
				{
					const auto action = updates_disabled
						? "Relaunch S2x without -noupdate so it can download a valid runtime."
						: "Check your connection and relaunch S2x so it can update the runtime.";
					throw std::runtime_error(utils::string::va(
						"The Microsoft Store/Xbox runtime '%s' could not be loaded.\n\n%s",
						binary_to_load.data(), action
					));
				}

				throw std::runtime_error(utils::string::va(
					"Failed to load '%s'.\n\n"
					"The game binary could not be loaded into memory. "
					"Please verify your game files through Steam and make sure the file is not blocked.",
					binary_to_load.data()
				));
			}

			if (!game::is_valid_binary())
			{
				if (game::environment::is_microsoft_store())
				{
					const auto action = updates_disabled
						? "Relaunch S2x without -noupdate so it can download a supported runtime."
						: "Check your connection and relaunch S2x so it can update the runtime.";
					throw std::runtime_error(utils::string::va(
						"The Microsoft Store/Xbox runtime '%s' is not compatible with this version of S2x.\n\n%s",
						binary_to_load.data(), action
					));
				}

				throw std::runtime_error(
					"The game binary is not compatible with this version of S2x.\n\n"
					"Please update Call of Duty: WWII through Steam and verify the integrity of the game files."
				);
			}

			if (store_runtime_update_warning)
			{
				console::warn("[Updater] Store runtime update check failed: %s\n"
					"[Updater] Continuing with the compatible local runtime '%s'.\n",
					store_runtime_update_warning->data(), binary_to_load.data());
			}

			patch_imports();

			if (!component_loader::post_load())
			{
				return 1;
			}

			premature_shutdown = false;
		}
		catch (std::exception& e)
		{
			MessageBoxA(nullptr, e.what(), "ERROR", MB_ICONERROR);
			return 1;
		}
	}

	g_call_tls_callbacks = true;
	return static_cast<int>(entry_point());
}

int __stdcall WinMain(HINSTANCE, HINSTANCE, PSTR, int)
{
	return main();
}
