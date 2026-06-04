#include <std_include.hpp>

#include "loader/component_loader.hpp"
#include "loader/loader.hpp"

#include <utils/finally.hpp>
#include <utils/hook.hpp>
#include <utils/nt.hpp>
#include <utils/io.hpp>
#include <utils/flags.hpp>
#include <utils/string.hpp>

#include <steam/steam.hpp>

#include "game/game.hpp"
#include "component/console/console.hpp"

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

	void patch_steam_import(const std::string& func, void* function)
	{
		static const utils::nt::library game{};

		const auto game_entry = game.get_iat_entry("steam_api64.dll", func);
		if (!game_entry)
		{
			return;
		}

		utils::hook::set(game_entry, function);
	}

	void patch_imports()
	{
		patch_steam_import("SteamAPI_RegisterCallback", steam::SteamAPI_RegisterCallback);
		patch_steam_import("SteamAPI_RegisterCallResult", steam::SteamAPI_RegisterCallResult);
		patch_steam_import("SteamGameServer_Shutdown", steam::SteamGameServer_Shutdown);
		patch_steam_import("SteamGameServer_RunCallbacks", steam::SteamGameServer_RunCallbacks);
		patch_steam_import("SteamGameServer_GetHSteamPipe", steam::SteamGameServer_GetHSteamPipe);
		patch_steam_import("SteamGameServer_GetHSteamUser", steam::SteamGameServer_GetHSteamUser);
		patch_steam_import("SteamInternal_GameServer_Init", steam::SteamInternal_GameServer_Init);
		patch_steam_import("SteamAPI_UnregisterCallResult", steam::SteamAPI_UnregisterCallResult);
		patch_steam_import("SteamAPI_UnregisterCallback", steam::SteamAPI_UnregisterCallback);
		patch_steam_import("SteamAPI_RunCallbacks", steam::SteamAPI_RunCallbacks);
		patch_steam_import("SteamInternal_CreateInterface", steam::SteamInternal_CreateInterface);
		patch_steam_import("SteamInternal_ContextInit", steam::SteamInternal_ContextInit);
		patch_steam_import("SteamAPI_GetHSteamUser", steam::SteamAPI_GetHSteamUser);
		patch_steam_import("SteamAPI_GetHSteamPipe", steam::SteamAPI_GetHSteamPipe);
		patch_steam_import("SteamAPI_Init", steam::SteamAPI_Init);
		patch_steam_import("SteamAPI_Shutdown", steam::SteamAPI_Shutdown);
		patch_steam_import("SteamAPI_RestartAppIfNecessary", steam::SteamAPI_RestartAppIfNecessary);

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

	launcher::mode detect_mode_from_arguments()
	{
		if (utils::flags::has_flag("-dedicated"))
		{
			return launcher::mode::server;
		}

		if (utils::flags::has_flag("-multiplayer"))
		{
			return launcher::mode::multiplayer;
		}

		if (utils::flags::has_flag("-singleplayer"))
		{
			return launcher::mode::singleplayer;
		}

		if (utils::flags::has_flag("-zombies"))
		{
			return launcher::mode::zombies;
		}

		return launcher::mode::none;
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

	console::init();

	enable_dpi_awareness();

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
			remove_crash_file();
			//updater::update();

			auto mode = detect_mode_from_arguments();
			if (mode == launcher::mode::none)
			{
				const launcher launcher;
				mode = launcher.run();

				if (mode == launcher::mode::none) return 0;
			}

			game::environment::set_mode(mode);

			if (game::environment::is_zombies() && !has_zombies_argument())
			{
				utils::nt::relaunch_self("-zombies +zombiesMode 1");
				return 0;
			}

			const auto mp_binary = "s2_mp64_ship.exe"s;
			const auto sp_binary = "s2_sp64_ship.exe"s;

			const auto& binary_to_load = game::environment::is_mp() ? mp_binary : sp_binary;
			
			if (!utils::io::file_exists(binary_to_load))
			{
				throw std::runtime_error(utils::string::va(
					"Could not find '%s'.\n\n"
					"Make sure S2x.exe is placed in your Call of Duty: WWII installation folder.",
					binary_to_load.data()
				));
			}

			if (!component_loader::activate(game::environment::is_sp()))
			{
				return 1;
			}

			entry_point = load_process(binary_to_load);
			if (!entry_point)
			{
				throw std::runtime_error(utils::string::va(
					"Failed to load '%s'.\n\n"
					"The game binary could not be loaded into memory. "
					"Please verify your game files through Steam and make sure the file is not blocked.",
					binary_to_load.data()
				));
			}

			if (!game::is_valid_binary())
			{
				throw std::runtime_error(
					"The game binary is not compatible with this version of S2x.\n\n"
					"Please update Call of Duty: WWII through Steam and verify the integrity of the game files."
				);
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
