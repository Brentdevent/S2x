#include "nt.hpp"

#include <lmcons.h>

#include "string.hpp"

namespace utils::nt
{
	library library::load(const char* name)
	{
		return library(LoadLibraryA(name));
	}

	library library::load(const std::string& name)
	{
		return library::load(name.data());
	}

	library library::load(const std::filesystem::path& path)
	{
		return library::load(path.generic_string());
	}

	library library::get_by_address(const void* address)
	{
		HMODULE handle = nullptr;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		                   static_cast<LPCSTR>(address), &handle);
		return library(handle);
	}

	library::library()
		: module_(GetModuleHandleA(nullptr))
	{
	}

	library::library(const std::string& name)
		: module_(GetModuleHandleA(name.data()))
	{
	}

	library::library(const HMODULE handle)
		: module_(handle)
	{
	}

	bool library::operator==(const library& obj) const
	{
		return this->module_ == obj.module_;
	}

	library::operator bool() const
	{
		return this->is_valid();
	}

	library::operator HMODULE() const
	{
		return this->get_handle();
	}

	PIMAGE_NT_HEADERS library::get_nt_headers() const
	{
		if (!this->is_valid()) return nullptr;
		return reinterpret_cast<PIMAGE_NT_HEADERS>(this->get_ptr() + this->get_dos_header()->e_lfanew);
	}

	PIMAGE_DOS_HEADER library::get_dos_header() const
	{
		return reinterpret_cast<PIMAGE_DOS_HEADER>(this->get_ptr());
	}

	PIMAGE_OPTIONAL_HEADER library::get_optional_header() const
	{
		if (!this->is_valid()) return nullptr;
		return &this->get_nt_headers()->OptionalHeader;
	}

	void** library::get_iat_entry(const std::string& module_name, std::string proc_name) const
	{
		return this->get_iat_entry(module_name, proc_name.data());
	}

	std::vector<PIMAGE_SECTION_HEADER> library::get_section_headers() const
	{
		std::vector<PIMAGE_SECTION_HEADER> headers;

		auto nt_headers = this->get_nt_headers();
		auto section = IMAGE_FIRST_SECTION(nt_headers);

		for (uint16_t i = 0; i < nt_headers->FileHeader.NumberOfSections; ++i, ++section)
		{
			if (section) headers.push_back(section);
			else OutputDebugStringA("There was an invalid section :O");
		}

		return headers;
	}

	std::uint8_t* library::get_ptr() const
	{
		return reinterpret_cast<std::uint8_t*>(this->module_);
	}

	void library::unprotect() const
	{
		if (!this->is_valid()) return;

		DWORD protection;
		VirtualProtect(this->get_ptr(), this->get_optional_header()->SizeOfImage, PAGE_EXECUTE_READWRITE,
		               &protection);
	}

	size_t library::get_relative_entry_point() const
	{
		if (!this->is_valid()) return 0;
		return this->get_nt_headers()->OptionalHeader.AddressOfEntryPoint;
	}

	void* library::get_entry_point() const
	{
		if (!this->is_valid()) return nullptr;
		return this->get_ptr() + this->get_relative_entry_point();
	}

	bool library::is_valid() const
	{
		return this->module_ != nullptr && this->get_dos_header()->e_magic == IMAGE_DOS_SIGNATURE;
	}

	std::string library::get_name() const
	{
		if (!this->is_valid()) return {};

		const auto path = this->get_path();
		const auto pos = path.generic_string().find_last_of("/\\");
		if (pos == std::string::npos) return path.generic_string();

		return path.generic_string().substr(pos + 1);
	}

	std::filesystem::path library::get_path() const
	{
		if (!this->is_valid()) return {};

		wchar_t name[MAX_PATH] = {0};
		GetModuleFileNameW(this->module_, name, MAX_PATH);

		return {name};
	}

	std::filesystem::path library::get_folder() const
	{
		if (!this->is_valid()) return {};

		const auto path = std::filesystem::path(this->get_path());
		return path.parent_path().generic_string();
	}

	void library::free()
	{
		if (this->is_valid())
		{
			FreeLibrary(this->module_);
			this->module_ = nullptr;
		}
	}

	HMODULE library::get_handle() const
	{
		return this->module_;
	}

	void** library::get_iat_entry(const std::string& module_name, const char* proc_name) const
	{
		if (!this->is_valid()) return nullptr;

		const library other_module(module_name);
		if (!other_module.is_valid()) return nullptr;

		auto* const target_function = other_module.get_proc<void*>(proc_name);
		if (!target_function) return nullptr;

		auto* header = this->get_optional_header();
		if (!header) return nullptr;

		auto* import_descriptor = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(this->get_ptr() + header->DataDirectory
			[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

		while (import_descriptor->Name)
		{
			if (!_stricmp(reinterpret_cast<char*>(this->get_ptr() + import_descriptor->Name), module_name.data()))
			{
				auto* original_thunk_data = reinterpret_cast<PIMAGE_THUNK_DATA>(import_descriptor->
					OriginalFirstThunk + this->get_ptr());
				auto* thunk_data = reinterpret_cast<PIMAGE_THUNK_DATA>(import_descriptor->FirstThunk + this->
					get_ptr());

				while (original_thunk_data->u1.AddressOfData)
				{
					if (thunk_data->u1.Function == reinterpret_cast<uint64_t>(target_function))
					{
						return reinterpret_cast<void**>(&thunk_data->u1.Function);
					}

					const size_t ordinal_number = original_thunk_data->u1.AddressOfData & 0xFFFFFFF;

					if (ordinal_number <= 0xFFFF)
					{
						auto* proc = GetProcAddress(other_module.module_, reinterpret_cast<char*>(ordinal_number));
						if (reinterpret_cast<void*>(proc) == target_function)
						{
							return reinterpret_cast<void**>(&thunk_data->u1.Function);
						}
					}

					++original_thunk_data;
					++thunk_data;
				}

				//break;
			}

			++import_descriptor;
		}

		return nullptr;
	}

	std::string library::get_nearest_export_name_by_address(const void* address, ptrdiff_t* offset) const
	{
		if (offset)
		{
			*offset = 0;
		}

		if (!this->is_valid() || !address)
		{
			return {};
		}

		const auto* opt = this->get_optional_header();
		if (!opt)
		{
			return {};
		}

		const auto& dir = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
		if (!dir.VirtualAddress || !dir.Size)
		{
			return {};
		}

		const auto base = this->get_ptr();

		const auto* export_dir =
			reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);

		const auto* functions =
			reinterpret_cast<const DWORD*>(base + export_dir->AddressOfFunctions);

		const auto* names =
			reinterpret_cast<const DWORD*>(base + export_dir->AddressOfNames);

		const auto* ordinals =
			reinterpret_cast<const WORD*>(base + export_dir->AddressOfNameOrdinals);

		const auto target = reinterpret_cast<uintptr_t>(address);
		const auto module_base = reinterpret_cast<uintptr_t>(base);

		const char* best_name = nullptr;
		uintptr_t best_address = 0;

		for (DWORD name_index = 0; name_index < export_dir->NumberOfNames; ++name_index)
		{
			const WORD function_index = ordinals[name_index];
			if (function_index >= export_dir->NumberOfFunctions)
			{
				continue;
			}

			const DWORD function_rva = functions[function_index];
			if (!function_rva)
			{
				continue;
			}

			const uintptr_t function_address = module_base + function_rva;

			if (function_address <= target && function_address >= best_address)
			{
				best_address = function_address;
				best_name = reinterpret_cast<const char*>(base + names[name_index]);
			}
		}

		if (!best_name)
		{
			return {};
		}

		if (offset)
		{
			*offset = static_cast<ptrdiff_t>(target - best_address);
		}

		return std::string(best_name);
	}

	registry_key open_or_create_registry_key(const HKEY base, const std::string& input)
	{
		const auto parts = string::split(input, '\\');

		registry_key current_key = base;

		for (const auto& part : parts)
		{
			registry_key new_key{};
			if (RegOpenKeyExA(current_key, part.data(), 0,
			                  KEY_ALL_ACCESS, &new_key) == ERROR_SUCCESS)
			{
				current_key = std::move(new_key);
				continue;
			}

			if (RegCreateKeyExA(current_key, part.data(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,
			                    nullptr, &new_key, nullptr) != ERROR_SUCCESS)
			{
				return {};
			}

			current_key = std::move(new_key);
		}

		return current_key;
	}

	bool is_wine()
	{
		static const auto has_wine_export = []() -> bool
		{
			const library ntdll("ntdll.dll");
			return ntdll.get_proc<void*>("wine_get_version");
		}();

		return has_wine_export;
	}

	bool is_shutdown_in_progress()
	{
		static auto* shutdown_in_progress = []
		{
			const library ntdll("ntdll.dll");
			return ntdll.get_proc<BOOLEAN(*)()>("RtlDllShutdownInProgress");
		}();

		return shutdown_in_progress();
	}

	void raise_hard_exception()
	{
		int data = false;
		const library ntdll("ntdll.dll");
		ntdll.invoke_pascal<void>("RtlAdjustPrivilege", 19, true, false, &data);
		ntdll.invoke_pascal<void>("NtRaiseHardError", 0xC000007B, 0, nullptr, nullptr, 6, &data);
		_Exit(0);
	}

	std::string load_resource(const int id)
	{
		const auto lib = library::get_by_address(load_resource);
		auto* const res = FindResource(lib, MAKEINTRESOURCE(id), RT_RCDATA);
		if (!res) return {};

		auto* const handle = LoadResource(lib, res);
		if (!handle) return {};

		return std::string(LPSTR(LockResource(handle)), SizeofResource(lib, res));
	}

	void relaunch_self(const std::string& extra_args)
	{
		const auto self = utils::nt::library::get_by_address(relaunch_self);

		STARTUPINFOA startup_info;
		PROCESS_INFORMATION process_info;

		ZeroMemory(&startup_info, sizeof(startup_info));
		ZeroMemory(&process_info, sizeof(process_info));
		startup_info.cb = sizeof(startup_info);

		char current_dir[MAX_PATH];
		GetCurrentDirectoryA(sizeof(current_dir), current_dir);
		
		std::string command_line = GetCommandLineA();

		if (!extra_args.empty())
		{
			command_line += " ";
			command_line += extra_args;
		}

		CreateProcessA(self.get_path().generic_string().data(), command_line.data(), nullptr, nullptr, false, NULL, nullptr,
		               current_dir,
		               &startup_info, &process_info);

		if (process_info.hThread && process_info.hThread != INVALID_HANDLE_VALUE) CloseHandle(process_info.hThread);
		if (process_info.hProcess && process_info.hProcess != INVALID_HANDLE_VALUE) CloseHandle(process_info.hProcess);
	}

	void terminate(const uint32_t code)
	{
		TerminateProcess(GetCurrentProcess(), code);
		_Exit(code);
	}

	std::string get_user_name()
	{
		char username[UNLEN + 1];
		DWORD username_len = UNLEN + 1;
		if (!GetUserNameA(username, &username_len))
		{
			return {};
		}

		return std::string(username, username_len - 1);
	}
}
