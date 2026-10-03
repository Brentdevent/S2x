#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "callstack.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>
#include <utils/thread.hpp>
#include <utils/compression.hpp>

#include <exception/minidump.hpp>

#include <version.hpp>

namespace exception
{
	namespace
	{
		DWORD main_thread_id{};

		thread_local struct
		{
			DWORD code = 0;
			PVOID address = nullptr;
		} exception_data{};

		struct
		{
			std::chrono::time_point<std::chrono::high_resolution_clock> last_recovery{};
			std::atomic<int> recovery_counts = {0};
		} recovery_data{};

		bool is_game_thread()
		{
			return main_thread_id == GetCurrentThreadId();
		}

		bool is_exception_interval_too_short()
		{
			const auto delta = std::chrono::high_resolution_clock::now() - recovery_data.last_recovery;
			return delta < 1min;
		}

		bool too_many_exceptions_occured()
		{
			return recovery_data.recovery_counts >= 3;
		}

		volatile bool& is_initialized()
		{
			static volatile bool initialized = false;
			return initialized;
		}

		bool is_recoverable()
		{
			return is_initialized()
				&& is_game_thread()
				&& !is_exception_interval_too_short()
				&& !too_many_exceptions_occured();
		}

		void show_mouse_cursor()
		{
			while (ShowCursor(TRUE) < 0);
		}

		void display_error_dialog()
		{
			const std::string error_str = utils::string::va("Fatal error (0x%08X) at 0x%p (0x%p, %s).\n"
			                                                "A minidump has been written.\n",
			                                                exception_data.code, exception_data.address,
				                                            game::derelocate(reinterpret_cast<uint64_t>(exception_data.address)),
				                                            game::environment::get_binary_string().data());

			utils::thread::suspend_other_threads();
			show_mouse_cursor();

			MessageBoxA(nullptr, error_str.data(), "S2x ERROR", MB_ICONERROR);
			TerminateProcess(GetCurrentProcess(), exception_data.code);
		}

		void reset_state()
		{
			if (is_recoverable())
			{
				recovery_data.last_recovery = std::chrono::high_resolution_clock::now();
				++recovery_data.recovery_counts;

				/*game::Com_Error(game::ERR_DROP, "Fatal error (0x%08X) at 0x%p (0x%p).\nA minidump has been written.\n\n"
				                "S2x has tried to recover your game, but it might not run stable anymore.\n\n"
				                "Make sure to update your graphics card drivers and install operating system updates!\n"
				                "Closing or restarting Steam might also help.",
				                exception_data.code, exception_data.address,
					            game::derelocate(reinterpret_cast<uint64_t>(exception_data.address)));*/
			}
			else
			{
				display_error_dialog();
			}
		}

		size_t get_reset_state_stub()
		{
			static auto* stub = utils::hook::assemble([](utils::hook::assembler& a)
			{
				a.sub(rsp, 0x10);
				a.or_(rsp, 0x8);
				a.jmp(reset_state);
			});

			return reinterpret_cast<size_t>(stub);
		}

		std::string get_timestamp()
		{
			tm ltime{};
			char timestamp[MAX_PATH] = {0};
			const auto time = _time64(nullptr);

			_localtime64_s(&ltime, &time);
			strftime(timestamp, sizeof(timestamp) - 1, "%Y-%m-%d-%H-%M-%S", &ltime);

			return timestamp;
		}

		const char* get_exception_string(const DWORD exception)
		{
#define EXCEPTION_CASE(CODE) case EXCEPTION_##CODE: return "EXCEPTION_" #CODE
			switch (exception)
			{
				EXCEPTION_CASE(ACCESS_VIOLATION);
				EXCEPTION_CASE(DATATYPE_MISALIGNMENT);
				EXCEPTION_CASE(BREAKPOINT);
				EXCEPTION_CASE(SINGLE_STEP);
				EXCEPTION_CASE(ARRAY_BOUNDS_EXCEEDED);
				EXCEPTION_CASE(FLT_DENORMAL_OPERAND);
				EXCEPTION_CASE(FLT_DIVIDE_BY_ZERO);
				EXCEPTION_CASE(FLT_INEXACT_RESULT);
				EXCEPTION_CASE(FLT_INVALID_OPERATION);
				EXCEPTION_CASE(FLT_OVERFLOW);
				EXCEPTION_CASE(FLT_STACK_CHECK);
				EXCEPTION_CASE(FLT_UNDERFLOW);
				EXCEPTION_CASE(INT_DIVIDE_BY_ZERO);
				EXCEPTION_CASE(INT_OVERFLOW);
				EXCEPTION_CASE(PRIV_INSTRUCTION);
				EXCEPTION_CASE(IN_PAGE_ERROR);
				EXCEPTION_CASE(ILLEGAL_INSTRUCTION);
				EXCEPTION_CASE(NONCONTINUABLE_EXCEPTION);
				EXCEPTION_CASE(STACK_OVERFLOW);
				EXCEPTION_CASE(INVALID_DISPOSITION);
				EXCEPTION_CASE(GUARD_PAGE);
				EXCEPTION_CASE(INVALID_HANDLE);
			default:
				return "UNKNOWN";
			}
#undef EXCEPTION_CASE
		}

		std::string get_registers(const CONTEXT& context)
		{
			std::string registers{};
			const auto add = [&registers](const char* name, const DWORD64 value)
			{
				registers.append(utils::string::va("\t%s = 0x%llX\r\n", name, value));
			};

			add("rax", context.Rax);
			add("rbx", context.Rbx);
			add("rcx", context.Rcx);
			add("rdx", context.Rdx);
			add("rsp", context.Rsp);
			add("rbp", context.Rbp);
			add("rsi", context.Rsi);
			add("rdi", context.Rdi);
			add("r8", context.R8);
			add("r9", context.R9);
			add("r10", context.R10);
			add("r11", context.R11);
			add("r12", context.R12);
			add("r13", context.R13);
			add("r14", context.R14);
			add("r15", context.R15);
			add("rip", context.Rip);

			return registers;
		}

		std::string generate_crash_info(const LPEXCEPTION_POINTERS exceptioninfo)
		{
			std::string info{};
			const auto line = [&info](const std::string& text)
			{
				info.append(text);
				info.append("\r\n");
			};

			const auto* record = exceptioninfo->ExceptionRecord;
			const auto address = reinterpret_cast<size_t>(record->ExceptionAddress);
			const auto exception_module = utils::nt::library::get_by_address(record->ExceptionAddress);

			line("S2x Crash Dump");
			line("");
			line("Version: "s + VERSION);
			line("Binary: "s + game::environment::get_binary_string());
			line("Mode: "s + game::environment::get_string());
			line("Timestamp: "s + get_timestamp());
			line(utils::string::va("Exception: 0x%08X (%s)", record->ExceptionCode, get_exception_string(record->ExceptionCode)));
			line(utils::string::va("Address: 0x%llX (%s)", address,
				exception_module ? exception_module.get_name().data() : "unknown"));
			line(utils::string::va("Base: 0x%llX", game::get_base()));
			line(utils::string::va("Thread: %u (%s)", GetCurrentThreadId(), is_game_thread() ? "main" : "auxiliary"));

			if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
			{
				line(utils::string::va("Access: %s 0x%llX",
					record->ExceptionInformation[0] == 1 ? "write to" : record->ExceptionInformation[0] == 8 ? "execute" : "read from",
					record->ExceptionInformation[1]));
			}

#pragma warning(push)
#pragma warning(disable: 4996)
			OSVERSIONINFOEXA version_info;
			ZeroMemory(&version_info, sizeof(version_info));
			version_info.dwOSVersionInfoSize = sizeof(version_info);
			GetVersionExA(reinterpret_cast<LPOSVERSIONINFOA>(&version_info));
#pragma warning(pop)

			line(utils::string::va("OS Version: %u.%u.%u%s", version_info.dwMajorVersion, version_info.dwMinorVersion,
				version_info.dwBuildNumber, utils::nt::is_wine() ? " (Wine)" : ""));
			line("");
			line("Call stack:");
			info.append(callstack::format(*exceptioninfo->ContextRecord));
			line("");
			line("Registers:");
			info.append(get_registers(*exceptioninfo->ContextRecord));

			return info;
		}

		void write_minidump(const LPEXCEPTION_POINTERS exceptioninfo)
		{
			const std::string crash_name = utils::string::va("minidumps/s2x-crash-%s.zip",
			                                                 get_timestamp().data());

			utils::compression::zip::archive zip_file{};
			zip_file.add("crash.dmp", create_minidump(exceptioninfo));
			zip_file.add("info.txt", generate_crash_info(exceptioninfo));
			zip_file.write(crash_name, "S2x Crash Dump");
		}

		bool is_harmless_error(const LPEXCEPTION_POINTERS exceptioninfo)
		{
			const auto code = exceptioninfo->ExceptionRecord->ExceptionCode;
			return code == STATUS_INTEGER_OVERFLOW || code == STATUS_FLOAT_OVERFLOW || code == STATUS_SINGLE_STEP;
		}

		LONG WINAPI exception_filter(const LPEXCEPTION_POINTERS exceptioninfo)
		{
			if (is_harmless_error(exceptioninfo))
			{
				return EXCEPTION_CONTINUE_EXECUTION;
			}

			write_minidump(exceptioninfo);

			exception_data.code = exceptioninfo->ExceptionRecord->ExceptionCode;
			exception_data.address = exceptioninfo->ExceptionRecord->ExceptionAddress;
			exceptioninfo->ContextRecord->Rip = get_reset_state_stub();

			return EXCEPTION_CONTINUE_EXECUTION;
		}

		void WINAPI set_unhandled_exception_filter_stub(LPTOP_LEVEL_EXCEPTION_FILTER)
		{
			// Don't register anything here...
		}
	}

	class component final : public generic_component
	{
	public:
		component()
		{
			main_thread_id = GetCurrentThreadId();
			SetUnhandledExceptionFilter(exception_filter);
		}

		void post_load() override
		{
			const utils::nt::library ntdll("ntdll.dll");
			auto* set_filter = ntdll.get_proc<void(*)(LPTOP_LEVEL_EXCEPTION_FILTER)>("RtlSetUnhandledExceptionFilter");

			set_filter(exception_filter);
			utils::hook::jump(set_filter, set_unhandled_exception_filter_stub);
		}
	};
}

REGISTER_COMPONENT(exception::component)
