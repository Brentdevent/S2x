#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console/console.hpp"
#include "scheduler.hpp"
#include "scripting.hpp"

#include "game/game.hpp"

#include "component/gsc/script_extension.hpp"
#include "component/gsc/script_loading.hpp"

#include <utils/io.hpp>
#include <utils/string.hpp>

namespace logfile
{
	namespace
	{
		game::dvar_t* logfile{};
		game::dvar_t* g_log{};

		std::int64_t level_start_time{};
		bool game_running{};

		void g_log_printf(const char* fmt, ...)
		{
			if (!game_running || game::virtual_lobby_loaded())
			{
				return;
			}

			if (!logfile || !logfile->current.enabled || !g_log || !g_log->current.string || !*g_log->current.string)
			{
				return;
			}

			va_list ap;
			va_start(ap, fmt);
			const auto length = _vscprintf(fmt, ap);
			va_end(ap);

			if (length < 0)
			{
				return;
			}

			std::string buffer(static_cast<std::size_t>(length), '\0');

			va_start(ap, fmt);
			vsnprintf(buffer.data(), buffer.size() + 1, fmt, ap);
			va_end(ap);

			const auto time = static_cast<int>(std::max<std::int64_t>(0, *game::mp::svs_time - level_start_time) / 1000);

			utils::io::write_file(g_log->current.string, std::format("{:3}:{}{} {}",
				time / 60,
				time % 60 / 10,
				time % 60 % 10,
				buffer
			), true);
		}

		void scr_log_print()
		{
			std::string buffer{};

			for (auto i = 0u; i < game::Scr_GetNumParam(); ++i)
			{
				buffer.append(game::Scr_GetString(i));
			}

			g_log_printf("%s", buffer.data());
		}

		void log_say(const int client_num, const command::params_sv& params)
		{
			const auto cmd = utils::string::to_lower(params[0]);
			if ((cmd != "say" && cmd != "say_team") || client_num < 0 || client_num >= *game::sv_maxclients)
			{
				return;
			}

			const auto* clients = *game::mp::svs_clients;
			if (!clients)
			{
				return;
			}

			auto message = params.join(1);
			while (!message.empty() && static_cast<unsigned char>(message.front()) < ' ')
			{
				message.erase(0, 1);
			}

			const auto& client = clients[client_num];
			g_log_printf("%s;%s;%i;%s;%s\n",
				cmd.data(),
				client.guid,
				client_num,
				client.name,
				message.data());
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			scheduler::once([]
			{
				logfile = game::Dvar_RegisterBool("logfile", true, game::DVAR_FLAG_NONE);
				g_log = game::Dvar_RegisterString("g_log", "s2x/logs/games_mp.log", game::DVAR_FLAG_NONE);
			}, scheduler::pipeline::main);

			gsc::override_function("logprint", scr_log_print);

			gsc::on_before_main([]
			{
				level_start_time = *game::mp::svs_time;
				game_running = true;

				console::info("------- Game Initialization -------\n");
				console::info("gamename: S2\n");
				console::info("gamedate: " __DATE__ "\n");

				if (!logfile || !logfile->current.enabled || !g_log || !g_log->current.string || !*g_log->current.string)
				{
					console::info("Not logging to disk.\n");
					return;
				}

				console::info("Logging to disk: '%s'.\n", g_log->current.string);
				g_log_printf("------------------------------------------------------------\n");
				g_log_printf("InitGame\n");
			});

			scripting::on_shutdown([](const int clear_scripts) -> void
			{
				if (!game_running)
				{
					return;
				}

				console::info("==== ShutdownGame (%d) ====\n", clear_scripts);

				g_log_printf("ShutdownGame:\n");
				g_log_printf("------------------------------------------------------------\n");

				game_running = false;
			});

			command::on_client_command(log_say);
		}
	};
}

REGISTER_COMPONENT(logfile::component)
