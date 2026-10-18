#include <std_include.hpp>
#include "process.hpp"

#include <utils/finally.hpp>
#include <utils/flags.hpp>
#include <utils/nt.hpp>

#include <charconv>
#include <system_error>

namespace pause::process
{
	namespace
	{
		constexpr auto helper_flag = "-pause-hard-helper";
		constexpr auto cancel_flag = "-pause-hard-cancel";

		// Native process-wide operations, as declared by the System Informer phnt headers.
		using process_function = NTSTATUS(NTAPI*)(HANDLE);

		std::mutex helper_mutex;
		utils::nt::handle<> helper_process;
		utils::nt::handle<> cancel_event;
		bool stopping{};

		// Borrowed from run_helper, used only by the helper's UI thread.
		thread_local HANDLE watched_process{};
		thread_local HANDLE watched_cancel{};

		void throw_last_error(const char* operation)
		{
			throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
		}

		HANDLE inherited_handle(const char* flag)
		{
			const auto value = utils::flags::get_value(flag);
			if (!value)
			{
				return nullptr;
			}

			std::uintptr_t number{};
			const auto [end, error] = std::from_chars(value->data(), value->data() + value->size(), number);
			if (error != std::errc{} || end != value->data() + value->size() || !number)
			{
				return nullptr;
			}

			const auto handle = reinterpret_cast<HANDLE>(number);
			DWORD flags{};
			return GetHandleInformation(handle, &flags) && (flags & HANDLE_FLAG_INHERIT) ? handle : nullptr;
		}

		int show_status_error(const wchar_t* action, const NTSTATUS status)
		{
			wchar_t text[256]{};
			swprintf_s(text, L"Could not %s the game (0x%08lX).", action, static_cast<unsigned long>(status));
			return MessageBoxW(nullptr, text, L"S2x hard pause", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
		}

		void CALLBACK check_game(HWND, UINT, UINT_PTR, DWORD)
		{
			const auto exited = watched_process && WaitForSingleObject(watched_process, 0) == WAIT_OBJECT_0;
			const auto cancelled = watched_cancel && WaitForSingleObject(watched_cancel, 0) == WAIT_OBJECT_0;
			if (!exited && !cancelled)
			{
				return;
			}

			// This thread only opens message boxes. Never touch the suspended game's windows.
			EnumThreadWindows(GetCurrentThreadId(), [](const HWND window, LPARAM) -> BOOL
			{
				if (IsWindowVisible(window))
				{
					PostMessageW(window, WM_CLOSE, 0, 0);
				}

				return TRUE;
			}, 0);
		}
	}

	std::optional<int> run_helper()
	{
		if (!utils::flags::has_flag(helper_flag))
		{
			return {};
		}

		const auto process_handle = inherited_handle(helper_flag);
		const auto cancel_handle = inherited_handle(cancel_flag);
		const auto process_id = process_handle ? GetProcessId(process_handle) : 0;
		if (!process_id || process_id == GetCurrentProcessId() || !cancel_handle || process_handle == cancel_handle)
		{
			return 1;
		}

		const utils::nt::handle<> process{process_handle};
		const utils::nt::handle<> cancel{cancel_handle};

		const utils::nt::library ntdll{"ntdll.dll"};
		const auto suspend = ntdll.get_proc<process_function>("NtSuspendProcess");
		const auto resume = ntdll.get_proc<process_function>("NtResumeProcess");
		if (!suspend || !resume)
		{
			MessageBoxW(nullptr, L"Process suspension is unavailable on this version of Windows.",
				L"S2x hard pause", MB_ICONERROR);
			return 1;
		}

		if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT || WaitForSingleObject(cancel, 0) != WAIT_TIMEOUT)
		{
			return 0;
		}

		const auto timer = SetTimer(nullptr, 0, 250, check_game);
		if (!timer)
		{
			MessageBoxW(nullptr, L"Could not start hard pause. The game is still running.",
				L"S2x hard pause", MB_ICONERROR);
			return 1;
		}

		watched_process = process;
		watched_cancel = cancel;
		const auto timer_cleanup = utils::finally([timer]
		{
			KillTimer(nullptr, timer);
			watched_process = nullptr;
			watched_cancel = nullptr;
		});

		if (const auto status = suspend(process); status < 0)
		{
			show_status_error(L"suspend", status);
			return 1;
		}

		auto resume_on_exit = utils::finally([&process, resume]
		{
			resume(process);
		});

		const auto result = MessageBoxW(nullptr, L"pause_hard", L"S2x", MB_OK | MB_SETFOREGROUND);

		// A cancellation must not dismiss a resume error before the player can retry.
		watched_cancel = nullptr;
		while (WaitForSingleObject(process, 0) != WAIT_OBJECT_0)
		{
			const auto status = resume(process);
			if (status >= 0)
			{
				break;
			}

			if (!show_status_error(L"resume", status))
			{
				return 1;
			}
		}

		resume_on_exit.cancel();
		return result ? 0 : 1;
	}

	bool start()
	{
		const std::lock_guard lock{helper_mutex};
		if (stopping || (helper_process && WaitForSingleObject(helper_process, 0) != WAIT_OBJECT_0))
		{
			return false;
		}

		SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
		utils::nt::handle<> cancel = CreateEventW(&security, TRUE, FALSE, nullptr);
		if (!cancel)
		{
			throw_last_error("CreateEvent");
		}

		HANDLE inherited_process{};
		if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &inherited_process,
			PROCESS_SUSPEND_RESUME | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, TRUE, 0))
		{
			throw_last_error("DuplicateHandle");
		}

		const utils::nt::handle<> process{inherited_process};

		SIZE_T size{};
		InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
		std::vector<std::uint8_t> storage(size);
		auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
		if (!InitializeProcThreadAttributeList(attributes, 1, 0, &size))
		{
			throw_last_error("InitializeProcThreadAttributeList");
		}

		const auto attribute_cleanup = utils::finally([attributes]
		{
			DeleteProcThreadAttributeList(attributes);
		});

		// Inherit only the game handle and shutdown event, never arbitrary game handles.
		HANDLE handles[]{process, cancel};
		if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
			handles, sizeof(handles), nullptr, nullptr))
		{
			throw_last_error("UpdateProcThreadAttribute");
		}

		const auto executable = utils::nt::library::get_by_address(start).get_path().wstring();
		auto command_line = L"\"" + executable + L"\" -pause-hard-helper " +
			std::to_wstring(reinterpret_cast<std::uintptr_t>(inherited_process)) + L" -pause-hard-cancel " +
			std::to_wstring(reinterpret_cast<std::uintptr_t>(static_cast<HANDLE>(cancel)));

		STARTUPINFOEXW startup{};
		startup.StartupInfo.cb = sizeof(startup);
		startup.lpAttributeList = attributes;

		PROCESS_INFORMATION child{};
		if (!CreateProcessW(executable.c_str(), command_line.data(), nullptr, nullptr, TRUE,
			EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &child))
		{
			throw_last_error("CreateProcess");
		}

		const utils::nt::handle<> thread{child.hThread};
		helper_process = child.hProcess;
		cancel_event = std::move(cancel);
		AllowSetForegroundWindow(child.dwProcessId);
		return true;
	}

	void stop()
	{
		const std::lock_guard lock{helper_mutex};
		stopping = true;
		if (cancel_event)
		{
			SetEvent(cancel_event);
		}
	}
}
