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
		launcher::mode mode = launcher::mode::none;

		launcher::mode translate_surrogate(const launcher::mode _mode)
		{
			switch (_mode)
			{
			case launcher::mode::zombies:
				return launcher::mode::multiplayer;
			default:
				return _mode;
			}
		}

		launcher::mode get_real_mode()
		{
			return mode;
		}

		launcher::mode get_mode()
		{
			return translate_surrogate(get_real_mode());
		}

		bool is_sp()
		{
			return get_mode() == launcher::mode::singleplayer;
		}

		bool is_mp()
		{
			return get_mode() == launcher::mode::multiplayer;
		}

		bool is_zombies()
		{
			return get_real_mode() == launcher::mode::zombies;
		}

		bool is_dedi()
		{
			static const auto dedicated = utils::flags::has_flag("-dedicated");
			return dedicated;
		}

		void set_mode(const launcher::mode _mode)
		{
			mode = _mode;
		}

		std::string get_string()
		{
			const auto current_mode = get_real_mode();
			switch (current_mode)
			{
			case launcher::mode::server:
				return "Dedicated Server";

			case launcher::mode::multiplayer:
				return "Multiplayer";

			case launcher::mode::singleplayer:
				return "Singleplayer";

			case launcher::mode::none:
				return "None";

			default:
				return "Unknown (" + std::to_string(static_cast<int>(mode)) + ")";
			}
		}
	}

	bool is_valid_binary()
	{
		auto mode = environment::get_mode();
		if (mode == launcher::mode::singleplayer)
		{
			return is_valid_singleplayer_binary();
		}

		return is_valid_multiplayer_binary();
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
		if (game::environment::is_sp())
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

	bool is_server_running()
	{
		const auto* com_sv_running = game::Dvar_FindMalleableVar("1080"); // com_sv_running

		return com_sv_running &&
			com_sv_running->current.enabled &&
			game::SV_Loaded() &&
			!*game::virtualLobby_Loaded;
	}
}
