#include <std_include.hpp>

#include "loader/component_loader.hpp"
#include "game.hpp"

#include <utils/finally.hpp>
#include <utils/flags.hpp>
#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace game
{
	namespace
	{
		const utils::nt::library& get_host_library()
		{
			static const auto host_library = []
			{
				utils::nt::library host{};
				if (!host || host == utils::nt::library::get_by_address(get_base))
				{
					throw std::runtime_error("Invalid host application");
				}

				return host;
			}();

			return host_library;
		}

		bool is_valid_multiplayer_binary()
		{
			return get_host_library().get_optional_header()->CheckSum == 0x020d9b94;
		}

		bool is_valid_singleplayer_binary()
		{
			return get_host_library().get_optional_header()->CheckSum == 0x01743a38;
		}

		constexpr std::uint32_t store_native_timestamp = 0x67467ACE;
		constexpr std::uint32_t store_native_image_size = 0x12967000;

		bool is_supported_store_binary(const std::uint32_t timestamp, const std::uint32_t image_size)
		{
			return timestamp == store_native_timestamp && image_size == store_native_image_size;
		}

		bool is_valid_store_native_binary()
		{
			const auto& host = get_host_library();
			return is_supported_store_binary(host.get_nt_headers()->FileHeader.TimeDateStamp,
				host.get_optional_header()->SizeOfImage);
		}

		constexpr size_t store_cgs_global_rva = 0x9E11A78;
		constexpr size_t store_cgs_stride = 0x16F38;
		constexpr size_t store_client_active_global_rva = 0x115ADA0;
		constexpr size_t store_client_active_stride = 0x165A0;

		const std::byte* read_store_global_array(const size_t global_rva, const size_t stride, const int index)
		{
			const auto* base = *reinterpret_cast<const std::byte* const*>(relocate(global_rva));
			return base ? base + stride * static_cast<size_t>(index) : nullptr;
		}

		const symbol<const std::byte*(int localClientNum)> cg_get_local_client_static{ 0x461E0, 0 };
		const symbol<const std::byte*(int localClientNum)> cl_get_local_client_active{ 0x795D0, 0 };
	}

	size_t get_base()
	{
		static const auto base = reinterpret_cast<size_t>(get_host_library().get_ptr());
		return base;
	}

	namespace environment
	{
		namespace
		{
			constexpr online_mode_info multiplayer_mode_info{
				"mp",
				"dm",
				18,
				2,
				1,
			};

			constexpr online_mode_info zombies_mode_info{
				"zm",
				"zombies",
				4,
				1,
				2,
			};

			platform current_platform = platform::steam;
			mode current_mode = mode::singleplayer;
			bool dedicated = false;
			bool store_native = false;
		}

		bool is_store_native()
		{
			return store_native;
		}

		void set_store_native(const bool is_store_native)
		{
			store_native = is_store_native;
		}

		std::string get_binary_string()
		{
			if (get_platform() == platform::steam)
			{
				return "Steam";
			}

			if (!is_store_native())
			{
				return "Steam (S2x runtime)";
			}

			const auto& host = get_host_library();
			return utils::string::va("Microsoft Store (%08X-%08X)",
				host.get_nt_headers()->FileHeader.TimeDateStamp,
				host.get_optional_header()->SizeOfImage);
		}

		platform get_platform()
		{
			return current_platform;
		}

		void set_platform(const platform new_platform)
		{
			current_platform = new_platform;
		}

		bool is_microsoft_store()
		{
			return get_platform() == platform::microsoft_store;
		}

		mode get_mode()
		{
			return current_mode;
		}

		void set_mode(const mode new_mode)
		{
			if (new_mode == mode::singleplayer && dedicated)
			{
				throw std::runtime_error("Dedicated Singleplayer is unsupported");
			}

			current_mode = new_mode;
		}

		bool is_dedicated()
		{
			return dedicated;
		}

		void set_dedicated(const bool is_dedicated)
		{
			if (is_dedicated && is_singleplayer())
			{
				throw std::runtime_error("Dedicated Singleplayer is unsupported");
			}

			dedicated = is_dedicated;
		}

		bool is_singleplayer()
		{
			return get_mode() == mode::singleplayer;
		}

		bool is_multiplayer()
		{
			return get_mode() == mode::multiplayer;
		}

		bool is_zombies()
		{
			return get_mode() == mode::zombies;
		}

		bool uses_multiplayer_binary()
		{
			return is_multiplayer() || is_zombies();
		}

		const online_mode_info& get_online_mode_info()
		{
			switch (get_mode())
			{
			case mode::multiplayer:
				return multiplayer_mode_info;

			case mode::zombies:
				return zombies_mode_info;

			case mode::singleplayer:
				throw std::logic_error("Singleplayer has no online mode information");
			}

			throw std::logic_error("Unknown gameplay mode");
		}

		std::string get_string()
		{
			switch (get_mode())
			{
			case mode::singleplayer:
				return "Singleplayer";

			case mode::multiplayer:
				return is_dedicated() ? "Dedicated Server" : "Multiplayer";

			case mode::zombies:
				return is_dedicated() ? "Zombies Dedicated Server" : "Zombies";
			}

			return "Unknown (" + std::to_string(static_cast<int>(get_mode())) + ")";
		}
	}

	bool is_valid_binary()
	{
		if (environment::is_store_native())
		{
			return environment::uses_multiplayer_binary() && is_valid_store_native_binary();
		}

		return environment::uses_multiplayer_binary()
			? is_valid_multiplayer_binary()
			: is_valid_singleplayer_binary();
	}

	bool is_supported_store_binary_file(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			return false;
		}

		IMAGE_DOS_HEADER dos_header{};
		if (!file.read(reinterpret_cast<char*>(&dos_header), sizeof(dos_header))
			|| dos_header.e_magic != IMAGE_DOS_SIGNATURE)
		{
			return false;
		}

		IMAGE_NT_HEADERS64 nt_headers{};
		if (!file.seekg(dos_header.e_lfanew)
			|| !file.read(reinterpret_cast<char*>(&nt_headers), sizeof(nt_headers))
			|| nt_headers.Signature != IMAGE_NT_SIGNATURE
			|| nt_headers.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
		{
			return false;
		}

		return is_supported_store_binary(nt_headers.FileHeader.TimeDateStamp, nt_headers.OptionalHeader.SizeOfImage);
	}

	std::filesystem::path get_appdata_path()
	{
		static const auto appdata_path = []
		{
			PWSTR path;
			if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)))
			{
				throw std::runtime_error("Failed to read APPDATA path!");
			}

			auto _ = utils::finally([&path]
			{
				CoTaskMemFree(path);
			});

			static auto appdata = std::filesystem::path(path) / "s2x";
			return appdata;
		}();

		return appdata_path;
	}

	bool Cbuf_AddText(int localClientNum, const char* text)
	{
		if (!game::environment::uses_multiplayer_binary())
		{
			// function is not inlined in SP
			return utils::hook::invoke<bool>(select(0, 0, 0x1BDA30), localClientNum, text);
		}

		RtlEnterCriticalSection(193);

		if (((*text - 'P') & 0xDF) == 0)
		{
			const auto client_char = text[1];

			if (static_cast<unsigned char>(client_char - '0') <= 1)
			{
				localClientNum = client_char - '0';
				text += 2;

				while (*text == ' ')
				{
					++text;
				}
			}
		}

		auto& buf = cmd_textArray[localClientNum];
		const auto len = static_cast<int>(std::strlen(text));

		const auto added = buf.cmdsize + len < buf.maxsize;
		if (added)
		{
			std::memcpy(buf.data + buf.cmdsize, text, static_cast<size_t>(len + 1));
			buf.cmdsize += len;
		}

		RtlLeaveCriticalSection(193);

		return added;
	}

	void Cbuf_AddCall(void* function)
	{
		RtlEnterCriticalSection(193);

		auto count = *cmd_funcCount;
		if (count < 0x20)
		{
			cmd_funcArray[count] = function;
			*cmd_funcCount = count + 1;
		}

		RtlLeaveCriticalSection(193);
	}

	int Cmd_Argc()
	{
		const auto nesting = game::cmd_args->nesting;

		if (nesting < 0 || nesting >= 8)
		{
			return 0;
		}

		return game::cmd_args->argc[nesting];
	}

	bool is_server_running()
	{
		const auto* sv_running = game::Dvar_FindMalleableVar("sv_running");

		return sv_running &&
			sv_running->current.enabled &&
			game::SV_Loaded() &&
			!*game::virtualLobby_Loaded;
	}

	bool is_local_play()
	{
		const auto* systemlink = game::Dvar_FindMalleableVar("systemlink");
		return systemlink && systemlink->current.enabled;
	}

	bool virtual_lobby_loaded()
	{
		if (!game::environment::uses_multiplayer_binary())
		{
			// function checks wether we're in the main menu or mission_select
			return utils::hook::invoke<bool>(select(0, 0, 0x4B89B0));
		}

		return *game::virtualLobby_Loaded;
	}

	const std::byte* CG_GetLocalClientStatic(const int localClientNum)
	{
		if (environment::is_store_native())
		{
			return read_store_global_array(store_cgs_global_rva, store_cgs_stride, localClientNum);
		}

		return cg_get_local_client_static.call_safe(localClientNum);
	}

	const std::byte* CL_GetLocalClientActive(const int localClientNum)
	{
		if (environment::is_store_native())
		{
			return read_store_global_array(store_client_active_global_rva, store_client_active_stride, localClientNum);
		}

		return cl_get_local_client_active.call_safe(localClientNum);
	}

	namespace hks
	{
		cclosure* cclosure_Create(lua_function func)
		{
			const auto state = *game::hks::lui_lua_state;

			game::hks::hksi_lua_pushcclosure(state, func, 0, nullptr, 0, 0);

			auto* obj = state->m_apistack.top - 1;
			auto* closure = obj->v.cClosure;

			state->m_apistack.top--;

			return closure;
		}
	}
}


