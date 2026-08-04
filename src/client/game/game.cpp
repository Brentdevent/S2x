#include <std_include.hpp>

#include "loader/component_loader.hpp"
#include "game.hpp"

#include <utils/finally.hpp>
#include <utils/flags.hpp>
#include <utils/hook.hpp>

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

			mode current_mode = mode::singleplayer;
			bool dedicated = false;
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
		return environment::uses_multiplayer_binary()
			? is_valid_multiplayer_binary()
			: is_valid_singleplayer_binary();
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
			return utils::hook::invoke<bool>(0x1BDA30_g, localClientNum, text);
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
		const auto* com_sv_running = game::Dvar_FindMalleableVar("com_sv_running");

		return com_sv_running &&
			com_sv_running->current.enabled &&
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
			return utils::hook::invoke<bool>(0x4B89B0_g);
		}

		return *game::virtualLobby_Loaded;
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


