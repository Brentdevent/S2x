#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "anti_debug_locations.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>
#include <mutex>

#include <process.h>

#define ProcessDebugPort 7
#define ProcessDebugObjectHandle 30
#define ProcessDebugFlags 31
#define ProcessImageFileNameWin32 43

#define STATUS_SUCCESS ((NTSTATUS)0x00000000)
#define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004L)

namespace arxan::anti_debug
{
	namespace
	{
		typedef enum _THREADINFOCLASS
		{
			ThreadBasicInformation = 0,
			ThreadHideFromDebugger = 17,
		} THREADINFOCLASS;

		utils::hook::detour nt_close_hook;
		utils::hook::detour nt_query_system_information_hook;
		utils::hook::detour nt_query_information_process_hook;
		utils::hook::detour nt_set_information_thread_hook;
		utils::hook::detour nt_query_information_thread_hook;
		utils::hook::detour create_mutex_ex_a_hook;
		utils::hook::detour get_thread_context_hook;
		utils::hook::detour resume_thread_hook;
		utils::hook::detour virtual_alloc_hook;
		utils::hook::detour check_remote_debugger_present_hook;
		utils::hook::detour enum_windows_hook;

		std::uint8_t* ntdll_code_address{};
		size_t ntdll_code_size{};

		PIMAGE_TLS_CALLBACK original_first_tls_callback = nullptr;
		decltype(&_beginthread) original_begin_thread = nullptr;
		void** begin_thread_import = nullptr;

		[[noreturn]] void fail_startup(const char* reason)
		{
			char message[1024]{};
			std::snprintf(message, sizeof(message), "S2x startup failed (%s).\r\n\r\n%s\r\n",
				game::environment::uses_multiplayer_binary() ? "MP" : "SP", reason);

			const auto error_output = GetStdHandle(STD_ERROR_HANDLE);
			if (error_output && error_output != INVALID_HANDLE_VALUE)
			{
				DWORD written{};
				WriteFile(error_output, message, static_cast<DWORD>(std::strlen(message)), &written, nullptr);
			}

			utils::nt::terminate_with_error(message, !game::environment::is_dedicated());
		}

		void disable_tls_callbacks()
		{
			auto* tls_callbacks = utils::nt::library{}.get_tls_callbacks();
			if (!tls_callbacks)
			{
				return;
			}

			original_first_tls_callback = tls_callbacks[0];
			utils::hook::set(&tls_callbacks[0], nullptr);
		}

		void restore_tls_callbacks()
		{
			auto* tls_callbacks = utils::nt::library{}.get_tls_callbacks();
			if (!tls_callbacks)
			{
				return;
			}

			utils::hook::set(&tls_callbacks[0], original_first_tls_callback);
		}

		uintptr_t __cdecl begin_thread_stub(void(__cdecl* start_address)(void*), const unsigned stack_size,
			void* const parameter)
		{
			// Restore TLS before the first game thread starts.
			restore_tls_callbacks();
			utils::hook::set(begin_thread_import, original_begin_thread);
			return original_begin_thread(start_address, stack_size, parameter);
		}

		bool disable_debug_function_patches()
		{
			// Fail VirtualProtect so the game skips installing each debugger hook.
			constexpr uint8_t failure[] =
			{
				0x31, 0xC0, // xor eax, eax
				0x90, 0x90, 0x90, 0x90, // nop padding
			};
			constexpr uint8_t branch[] = {0x85, 0xC0, 0x0F, 0x84}; // test eax, eax; jz rel32
			const auto mp_binary = game::environment::uses_multiplayer_binary();
			const auto dispatch = reinterpret_cast<void*>(
				game::select(mp::guard_dispatch_icall, sp::guard_dispatch_icall));

			// Validate all sites before patching the decrypted initializer.
			for (const auto& site : debug_function_patch_sites)
			{
				auto* const code = reinterpret_cast<uint8_t*>(game::relocate(mp_binary ? site.mp : site.sp));
				const auto patched = std::memcmp(code, failure, sizeof(failure)) == 0;

				if (std::memcmp(code + sizeof(failure), branch, sizeof(branch)) != 0 ||
					(!patched && (code[0] != 0xFF || code[1] != 0x15 ||
						utils::hook::extract<void*>(code + 2) != dispatch)))
				{
					return false;
				}
			}

			for (const auto& site : debug_function_patch_sites)
			{
				utils::hook::copy(game::relocate(mp_binary ? site.mp : site.sp), failure, sizeof(failure));
			}

			return true;
		}

		void disable_worker()
		{
			static bool disabled{};
			if (disabled)
			{
				return;
			}

			const auto mp_binary = game::environment::uses_multiplayer_binary();
			const auto& calls = mp_binary
				? mp::worker_thread_calls : sp::worker_thread_calls;
			const auto dispatch = reinterpret_cast<void*>(
				game::select(mp::guard_dispatch_icall, sp::guard_dispatch_icall));
			auto* frame_check = reinterpret_cast<uint8_t*>(
				game::select(mp::worker_frame_check_offsets[0], sp::worker_frame_check_offsets[0]));
			const auto frame_loop = game::select(mp::worker_frame_loop, sp::worker_frame_loop);
			auto* status = reinterpret_cast<uint64_t*>(
				game::select(mp::worker_debug_status, sp::worker_debug_status));

			for (const auto rva : calls)
			{
				auto* code = reinterpret_cast<uint8_t*>(game::relocate(rva));
				if (code[0] != 0xFF || code[1] != 0x15 || utils::hook::extract<void*>(code + 2) != dispatch)
				{
					throw std::runtime_error("Unable to locate the Arxan worker startup calls");
				}
			}

			constexpr uint8_t frame_load[] = {0x48, 0x8B, 0x05};
			constexpr uint8_t frame_store[] = {0x48, 0x89, 0x44, 0x24, 0x30};
			if (std::memcmp(frame_check, frame_load, sizeof(frame_load)) != 0 ||
				std::memcmp(frame_check + 7, frame_store, sizeof(frame_store)) != 0 ||
				*status != 0xCEF43C2772A84800ull)
			{
				throw std::runtime_error("Unable to locate the Arxan worker state");
			}

			// Preserve the worker's bootstrap state without creating a thread.
			const auto imported_atoi = *reinterpret_cast<uintptr_t*>(
				game::select(mp::worker_atoi_import, sp::worker_atoi_import));
			utils::hook::set<uint64_t>(game::select(mp::worker_cookie, sp::worker_cookie),
				imported_atoi ^ 0xD092C15A145FC679ull);
			utils::hook::set(status, mp_binary ? 0x39FCB72AB77AEFFDull : 0x39FCB72AB77AEFFFull);

			constexpr uint8_t no_thread[] = {0x31, 0xC0, 0x90, 0x90, 0x90, 0x90}; // xor eax, eax
			for (const auto rva : calls)
			{
				utils::hook::copy(game::relocate(rva), no_thread, sizeof(no_thread));
			}

			// Continue the frame loop without querying or resuming the absent worker.
			utils::hook::jump(frame_check, reinterpret_cast<void*>(frame_loop));
			disabled = true;
		}

		DWORD WINAPI resume_thread_stub(const HANDLE thread)
		{
			const auto return_address = game::select(mp::worker_load_resume_return, sp::worker_load_resume_return);
			if (!thread && reinterpret_cast<uintptr_t>(_ReturnAddress()) == return_address)
			{
				// The encrypted map loader still probes the removed worker.
				return 0;
			}

			return resume_thread_hook.invoke<DWORD>(thread);
		}

		template <size_t SiteCount>
		void disable_being_debugged_checks(const uint64_t (&offsets)[SiteCount])
		{
			// Patch the read to avoid racing debugger attachment.
			constexpr uint8_t original[] =
			{
				0x65, 0x48, 0x8B, 0x04, 0x25, 0x60, 0x00, 0x00, 0x00, // mov rax, gs:[0x60]
				0x0F, 0xB6, 0x40, 0x02, // movzx eax, byte ptr [rax+2]
				0x85, 0xC0, // test eax, eax
			};

			for (const auto offset : offsets)
			{
				const auto* code = reinterpret_cast<const void*>(game::relocate(offset - 9));

				if (std::memcmp(code, original, sizeof(original)) != 0)
				{
					throw std::runtime_error(utils::string::va("Unable to locate BeingDebugged check: %llX", offset));
				}
			}

			// Keep the existing test and register restoration.
			constexpr uint8_t replacement[] =
			{
				0x31, 0xC0, // xor eax, eax
				0x90, 0x90, // nop padding
			};

			for (const auto offset : offsets)
			{
				utils::hook::copy(game::relocate(offset), replacement, sizeof(replacement));
			}
		}

		template <size_t SiteCount>
		void disable_nt_global_flag_checks(const uint64_t (&offsets)[SiteCount])
		{
			constexpr uint8_t original[] =
			{
				0x65, 0x48, 0x8B, 0x04, 0x25, 0x60, 0x00, 0x00, 0x00, // mov rax, gs:[0x60]
				0x8A, 0x80, 0xBC, 0x00, 0x00, 0x00, // mov al, [rax+0xBC]
				0x24, 0x70, // and al, 0x70
				0x3C, 0x70, // cmp al, 0x70
			};

			for (const auto offset : offsets)
			{
				const auto* code = reinterpret_cast<const void*>(game::relocate(offset - 9));
				if (std::memcmp(code, original, sizeof(original)) != 0)
				{
					throw std::runtime_error(utils::string::va("Unable to locate NtGlobalFlag check: %llX", offset));
				}
			}

			// Preserve upper RAX bits and let the existing comparison set flags.
			constexpr uint8_t replacement[] =
			{
				0xB0, 0x00, // mov al, 0
				0x90, 0x90, 0x90, 0x90, // nop padding
			};

			for (const auto offset : offsets)
			{
				utils::hook::copy(game::relocate(offset), replacement, sizeof(replacement));
			}
		}

		template <size_t SiteCount, size_t PatchSize>
		void patch_heap_checks(const uint64_t (&offsets)[SiteCount], const uint8_t (&original)[PatchSize],
			const uint8_t (&replacement)[PatchSize])
		{
			for (const auto offset : offsets)
			{
				const auto* code = reinterpret_cast<const void*>(game::relocate(offset));
				if (std::memcmp(code, original, PatchSize) != 0)
				{
					throw std::runtime_error(utils::string::va("Unable to locate heap debugger check: %llX", offset));
				}
			}

			for (const auto offset : offsets)
			{
				utils::hook::copy(game::relocate(offset), replacement, PatchSize);
			}
		}

		void disable_heap_debugger_checks()
		{
			// Keep EAX = HEAP_GROWABLE and the original success flags.
			constexpr uint8_t flags_original[] =
			{
				0x8B, 0x00, // mov eax, [rax]
				0xA9, 0xFD, 0xFF, 0xFF, 0xFF, // test eax, ~HEAP_GROWABLE
			};
			constexpr uint8_t flags_replacement[] =
			{
				0xB8, 0x02, 0x00, 0x00, 0x00, // mov eax, HEAP_GROWABLE
				0x39, 0xC0, // cmp eax, eax
			};

			constexpr uint8_t force_flags_original[] =
			{
				0x8B, 0x00, // mov eax, [rax]
				0x85, 0xC0, // test eax, eax
			};
			constexpr uint8_t force_flags_replacement[] =
			{
				0x31, 0xC0, // xor eax, eax
				0x90, 0x90, // nop padding
			};

			// Hide the debug heap tail marker.
			constexpr uint8_t tail_original[] =
			{
				0x8B, 0x00, // mov eax, [rax]
				0x3D, 0xAB, 0xAB, 0xAB, 0xAB, // cmp eax, 0xABABABAB
			};
			constexpr uint8_t tail_replacement[] =
			{
				0x31, 0xC0, // xor eax, eax
				0x3D, 0xAB, 0xAB, 0xAB, 0xAB, // cmp eax, 0xABABABAB
			};

			if (game::environment::uses_multiplayer_binary())
			{
				patch_heap_checks(mp::heap_flags_read_offsets, flags_original, flags_replacement);
				patch_heap_checks(mp::heap_force_flags_read_offsets, force_flags_original, force_flags_replacement);
				patch_heap_checks(mp::heap_tail_read_offsets, tail_original, tail_replacement);
			}
			else
			{
				patch_heap_checks(sp::heap_flags_read_offsets, flags_original, flags_replacement);
				patch_heap_checks(sp::heap_force_flags_read_offsets, force_flags_original, force_flags_replacement);
				patch_heap_checks(sp::heap_tail_read_offsets, tail_original, tail_replacement);

				// The SP Flags read and test are separated by an obfuscated jump.
				constexpr uint8_t split_original[] =
				{
					0x48, 0x8B, 0x45, 0x08, // mov rax, [rbp+8]
					0x8B, 0x00, // mov eax, [rax]
				};
				constexpr uint8_t split_replacement[] =
				{
					0xB8, 0x02, 0x00, 0x00, 0x00, // mov eax, HEAP_GROWABLE
					0x90, // nop
				};
				patch_heap_checks(sp::heap_flags_split_read_offsets, split_original, split_replacement);
			}
		}

		void disable_peb_debugger_checks()
		{
			static std::once_flag once;
			std::call_once(once, []
			{
				disable_heap_debugger_checks();
				if (game::environment::uses_multiplayer_binary())
				{
					disable_being_debugged_checks(mp::being_debugged_read_offsets);
					disable_nt_global_flag_checks(mp::nt_global_flag_read_offsets);
				}
				else
				{
					disable_being_debugged_checks(sp::being_debugged_read_offsets);
					disable_nt_global_flag_checks(sp::nt_global_flag_read_offsets);
				}
			});
		}

		HANDLE create_mutex_ex_a_stub(const LPSECURITY_ATTRIBUTES attributes, const LPCSTR name, const DWORD flags,
			const DWORD access)
		{
			if (name == "$ IDA trusted_idbs"s || name == "$ IDA registry mutex $"s)
			{
				return nullptr;
			}

			return create_mutex_ex_a_hook.invoke<HANDLE>(attributes, name, flags, access);
		}

		bool remove_evil_keywords_from_string(const UNICODE_STRING& string)
		{
			// Hook removal can query process names during static destruction.
			static constexpr std::wstring_view evil_keywords[] =
			{
				L"IDA",
				L"ida",
				L"HxD",
				L"cheatengine",
				L"Cheat Engine",
				L"x96dbg",
				L"x32dbg",
				L"x64dbg",
				L"Wireshark",
				L"DebugView",
				L"ReClass.NET (x64)",
				L"ReClass.NET",
			};

			if (!string.Buffer || string.Length == 0)
			{
				return false;
			}

			const size_t char_count = string.Length / sizeof(wchar_t);
			std::wstring_view path(string.Buffer, char_count);

			bool modified = false;
			for (const auto& keyword : evil_keywords)
			{
				while (true)
				{
					const auto pos = path.find(keyword);
					if (pos == std::wstring_view::npos)
					{
						break;
					}

					modified = true;

					for (size_t i = 0; i < keyword.size(); ++i)
					{
						string.Buffer[pos + i] = L'a';
					}
				}
			}

			return modified;
		}

		bool remove_evil_keywords_from_string(wchar_t* str, const size_t length)
		{
			if (!str || length == 0)
			{
				return false;
			}

			if (length > (std::numeric_limits<uint16_t>::max() / sizeof(wchar_t)))
			{
				return false;
			}

			UNICODE_STRING unicode_string{};
			unicode_string.Buffer = str;
			unicode_string.Length = static_cast<uint16_t>(length * sizeof(wchar_t));
			unicode_string.MaximumLength = unicode_string.Length;

			return remove_evil_keywords_from_string(unicode_string);
		}

		BOOL CALLBACK enum_windows_callback_nullsub(HWND hwnd, LPARAM)
		{
			return TRUE;
		}

		BOOL WINAPI enum_windows_stub(WNDENUMPROC callback, LPARAM lparam)
		{
			const auto ret = _ReturnAddress();

			const auto game = utils::nt::library{};
			const auto source_module = utils::nt::library::get_by_address(ret);

			if (source_module == game)
			{
				// Skip the game's window-title scan.
				return enum_windows_hook.invoke<BOOL>(enum_windows_callback_nullsub, lparam);
			}

			return enum_windows_hook.invoke<BOOL>(callback, lparam);
		}

		int WINAPI get_window_text_a_stub(const HWND wnd, const LPSTR str, const int max_count)
		{
			std::wstring wstr{};
			wstr.resize(max_count);

			const auto res = GetWindowTextW(wnd, &wstr[0], max_count);
			if (res)
			{
				remove_evil_keywords_from_string(wstr.data(), res);

				const std::string regular_str(wstr.begin(), wstr.end());
				memset(str, 0, max_count);
				memcpy(str, regular_str.data(), res);
			}

			return res;
		}

		int WINAPI get_system_metrics_stub(const int index)
		{
			if (SM_REMOTESESSION == index)
			{
				return 0;
			}

			return GetSystemMetrics(index);
		}

		NTSTATUS NTAPI nt_query_system_information_stub(const SYSTEM_INFORMATION_CLASS system_information_class,
			const PVOID system_information,
			const ULONG system_information_length,
			const PULONG return_length)
		{
			const auto status = nt_query_system_information_hook.invoke<NTSTATUS>(
				system_information_class, system_information, system_information_length, return_length);

			if (NT_SUCCESS(status))
			{
				if (system_information_class == SystemProcessInformation && !utils::nt::is_shutdown_in_progress())
				{
					auto addr = static_cast<uint8_t*>(system_information);

					while (true)
					{
						const auto info = reinterpret_cast<SYSTEM_PROCESS_INFORMATION*>(addr);
						remove_evil_keywords_from_string(info->ImageName);

						if (!info->NextEntryOffset)
						{
							break;
						}

						addr = addr + info->NextEntryOffset;
					}
				}
			}

			return status;
		}

		NTSTATUS WINAPI nt_query_information_process_stub(const HANDLE handle, const PROCESSINFOCLASS info_class,
			const PVOID info,
			const ULONG info_length, const PULONG ret_length)
		{
			const auto status = nt_query_information_process_hook.invoke<NTSTATUS>(handle, info_class, info, info_length,
				ret_length);

			if (NT_SUCCESS(status))
			{
				if (info_class == ProcessDebugObjectHandle)
				{
					*static_cast<HANDLE*>(info) = nullptr;
					return static_cast<LONG>(0xC0000353);
				}
				else if (info_class == ProcessImageFileName || static_cast<int>(info_class) ==
					ProcessImageFileNameWin32)
				{
					remove_evil_keywords_from_string(*static_cast<UNICODE_STRING*>(info));
				}
				else if (info_class == ProcessDebugPort)
				{
					*static_cast<HANDLE*>(info) = nullptr;
				}
				else if (info_class == ProcessDebugFlags)
				{
					*static_cast<ULONG*>(info) = 1;
				}
			}

			return status;
		}

		BOOL WINAPI check_remote_debugger_present_stub(HANDLE process, PBOOL debugger_present)
		{
			const auto result = check_remote_debugger_present_hook.invoke<BOOL>(process, debugger_present);

			if (debugger_present)
			{
				*debugger_present = FALSE;
			}

			return result;
		}

		BOOL WINAPI is_debugger_present_stub()
		{
			return FALSE;
		}

		NTSTATUS NTAPI nt_close_stub(const HANDLE handle)
		{
			char info[16];
			if (NtQueryObject(handle, OBJECT_INFORMATION_CLASS(4), &info, 2, nullptr) >= 0 && size_t(handle) != 0x12345)
			{
				return nt_close_hook.invoke<NTSTATUS>(handle);
			}

			return STATUS_INVALID_HANDLE;
		}

		LONG WINAPI exception_filter(const LPEXCEPTION_POINTERS info)
		{
			const auto rec = info->ExceptionRecord;
			if (rec->ExceptionCode == STATUS_INVALID_HANDLE)
			{
				return EXCEPTION_CONTINUE_EXECUTION;
			}

			return EXCEPTION_CONTINUE_SEARCH;
		}

		bool is_valid_thread_handle(const HANDLE handle)
		{
			if (handle == reinterpret_cast<HANDLE>(-2)) // NtCurrentThread
			{
				return true;
			}

			return GetThreadId(handle) != 0;
		}

		BOOL WINAPI get_thread_context_stub(const HANDLE thread_handle, const LPCONTEXT context)
		{
			const auto requested = context ? context->ContextFlags : 0;
			const auto result = get_thread_context_hook.invoke<BOOL>(thread_handle, context);

			if (result && context)
			{
				constexpr DWORD64 debug_flags = CONTEXT_DEBUG_REGISTERS;

				auto* source = _ReturnAddress();
				const auto game = utils::nt::library{};
				const auto source_module = utils::nt::library::get_by_address(source);

				// Hide debug registers from game callers.
				if (source_module == game && (requested & debug_flags))
				{
					context->Dr0 = 0;
					context->Dr1 = 0;
					context->Dr2 = 0;
					context->Dr3 = 0;
					context->Dr6 = 0;
					context->Dr7 = 0;
				}
			}

			return result;
		}

		NTSTATUS NTAPI nt_set_information_thread_stub(HANDLE thread_handle,
			THREADINFOCLASS thread_information_class,
			PVOID thread_information,
			ULONG thread_information_length)
		{
			if (thread_information_class == ThreadHideFromDebugger)
			{
				component_loader::post_thread_setup();

				// Preserve the error results used by the game's integrity checks.
				if (!is_valid_thread_handle(thread_handle))
				{
					return STATUS_INVALID_HANDLE;
				}

				if (thread_information_length != 0)
				{
					return STATUS_INFO_LENGTH_MISMATCH;
				}

				// Report success without hiding the thread.
				return STATUS_SUCCESS;
			}

			return nt_set_information_thread_hook.invoke<NTSTATUS>(
				thread_handle,
				thread_information_class,
				thread_information,
				thread_information_length
			);
		}

		NTSTATUS NTAPI nt_query_information_thread_stub(
			HANDLE thread_handle,
			THREADINFOCLASS thread_information_class,
			PVOID thread_information,
			ULONG thread_information_length,
			PULONG return_length
		)
		{
			if (thread_information_class == ThreadHideFromDebugger)
			{
				// ThreadHideFromDebugger returns one byte.
				if (thread_information_length != 1)
				{
					return STATUS_INFO_LENGTH_MISMATCH;
				}

				if (thread_information)
				{
					*static_cast<std::uint8_t*>(thread_information) = 1; // thread is hidden
				}

				if (return_length)
				{
					*return_length = 1;
				}

				return STATUS_SUCCESS;
			}

			return nt_query_information_thread_hook.invoke<NTSTATUS>(
				thread_handle,
				thread_information_class,
				thread_information,
				thread_information_length,
				return_length
			);
		}

		void initialize_ntdll_code_section(const utils::nt::library& ntdll, const void* nt_set_information_thread)
		{
			const auto* section = ntdll.get_section_header(nt_set_information_thread);
			if (!section || !(section->Characteristics & IMAGE_SCN_MEM_EXECUTE) || section->VirtualAddress == 0 ||
				section->SizeOfRawData == 0 || section->SizeOfRawData > ntdll.get_optional_header()->SizeOfImage - section->VirtualAddress)
			{
				throw std::runtime_error("Unable to locate the NTDLL executable section for game startup");
			}

			ntdll_code_address = ntdll.get_ptr() + section->VirtualAddress;
			// Match the game's use of SizeOfRawData.
			ntdll_code_size = section->SizeOfRawData;
		}

		LPVOID WINAPI virtual_alloc_stub(LPVOID address, SIZE_T size, DWORD allocation_type, DWORD protect)
		{
			const auto return_rva = game::derelocate(_ReturnAddress());
			const auto& sites = game::environment::uses_multiplayer_binary()
				? mp::ntdll_copy_sites : sp::ntdll_copy_sites;

			for (const auto& site : sites)
			{
				if (site.return_rva != return_rva)
				{
					continue;
				}

				auto* loop = reinterpret_cast<std::uint8_t*>(game::relocate(site.loop_rva));
				auto* resume = reinterpret_cast<void*>(game::relocate(site.resume_rva));
				const auto patched = loop[0] == 0xE9 && utils::hook::follow_branch(loop) == resume;
				const auto valid_request = address == nullptr && (allocation_type & MEM_COMMIT) != 0 &&
					(allocation_type & ~(MEM_COMMIT | MEM_RESERVE)) == 0 && protect == PAGE_EXECUTE_READWRITE;

				if (!valid_request || size != ntdll_code_size ||
					(!patched && std::memcmp(loop, site.original, sizeof(site.original)) != 0))
				{
					// Do not expose live code to an unmatched copy loop.
					fail_startup("The game's NTDLL copy routine or allocation does not match the expected startup layout.");
				}

				if (site.patches_debug_functions && !disable_debug_function_patches())
				{
					fail_startup("The game's debugger-function patch sites do not match the expected code.");
				}

				// Patch after decryption, before startup probes the debugger.
				try
				{
					disable_peb_debugger_checks();
					if (site.patches_debug_functions)
					{
						disable_worker();
					}
				}
				catch (const std::exception& e)
				{
					fail_startup(e.what());
				}

				// Each copy loop must be patched at its own allocation.
				if (!patched)
				{
					utils::hook::jump(loop, resume);
				}

				return ntdll_code_address;
			}

			return virtual_alloc_hook.invoke<LPVOID>(address, size, allocation_type, protect);
		}

		void install_thread_information_hooks(const utils::nt::library& ntdll)
		{
			resume_thread_hook.create(ResumeThread, resume_thread_stub);

			auto* get_thread_context_func = utils::nt::library("kernelbase.dll").get_proc<void*>("GetThreadContext");
			get_thread_context_hook.create(get_thread_context_func, get_thread_context_stub);

			const auto nt_set_information_thread = ntdll.get_proc<void*>("NtSetInformationThread");
			initialize_ntdll_code_section(ntdll, nt_set_information_thread);

			nt_set_information_thread_hook.create(nt_set_information_thread, nt_set_information_thread_stub);
			nt_set_information_thread_hook.move();

			const auto nt_query_information_thread = ntdll.get_proc<void*>("NtQueryInformationThread");
			nt_query_information_thread_hook.create(nt_query_information_thread, nt_query_information_thread_stub);
			nt_query_information_thread_hook.move();
		}
	}

	struct component final : generic_component
	{
	public:
		void post_load() override
		{
			const utils::nt::library ntdll("ntdll.dll");

			auto* dll_characteristics = &utils::nt::library().get_optional_header()->DllCharacteristics;
			utils::hook::set<WORD>(dll_characteristics, *dll_characteristics | IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE);

			disable_tls_callbacks();

			AddVectoredExceptionHandler(1, exception_filter);

			create_mutex_ex_a_hook.create(CreateMutexExA, create_mutex_ex_a_stub);

			const auto begin_thread_hook = utils::hook::iat(utils::nt::library{},
				"api-ms-win-crt-runtime-l1-1-0.dll", "_beginthread", begin_thread_stub);

			if (!begin_thread_hook)
			{
				throw std::runtime_error("Unable to hook game _beginthread import");
			}

			begin_thread_import = static_cast<void**>(begin_thread_hook->first);
			original_begin_thread = reinterpret_cast<decltype(original_begin_thread)>(begin_thread_hook->second);

			enum_windows_hook.create(EnumWindows, enum_windows_stub);

			nt_close_hook.create(ntdll.get_proc<void*>("NtClose"), nt_close_stub);

			// Hide debugger presence from the game without changing the PEB.
			auto* debugger_import = utils::nt::library{}.get_iat_entry("kernel32.dll", "IsDebuggerPresent");
			if (debugger_import) utils::hook::set(debugger_import, is_debugger_present_stub);

			const auto nt_query_system_information = ntdll.get_proc<void*>("NtQuerySystemInformation");
			nt_query_system_information_hook.create(nt_query_system_information, nt_query_system_information_stub);
			nt_query_system_information_hook.move();

			nt_query_information_process_hook.create(ntdll.get_proc<void*>("NtQueryInformationProcess"), nt_query_information_process_stub);

			utils::hook::jump(GetWindowTextA, get_window_text_a_stub, true, true);
			utils::hook::move_hook(GetWindowTextA);

			auto* sys_met_import = utils::nt::library{}.get_iat_entry("user32.dll", "GetSystemMetrics");
			if (sys_met_import) utils::hook::set(sys_met_import, get_system_metrics_stub);

			install_thread_information_hooks(ntdll);

			auto* virtual_alloc_func = utils::nt::library("kernel32.dll").get_proc<void*>("VirtualAlloc");
			virtual_alloc_hook.create(virtual_alloc_func, virtual_alloc_stub);

			auto* check_remote_debugger_present_func = utils::nt::library("kernel32.dll").get_proc<void*>("CheckRemoteDebuggerPresent");
			check_remote_debugger_present_hook.create(check_remote_debugger_present_func, check_remote_debugger_present_stub);
		}

		void post_unpack() override
		{
			disable_peb_debugger_checks();
		}

		component_priority priority() const override
		{
			return component_priority::arxan;
		}
	};
}

REGISTER_COMPONENT(arxan::anti_debug::component)
