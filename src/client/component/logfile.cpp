#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console/console.hpp"
#include "scheduler.hpp"
#include "scripting.hpp"

#include "game/game.hpp"

#include "component/gsc/script_extension.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>

namespace logfile
{
	namespace
	{
		constexpr auto mod_count = 0x16;

		game::dvar_t* logfile{};
		game::dvar_t* g_log{};

		utils::hook::detour scr_player_damage_hook;
		utils::hook::detour scr_player_killed_hook;

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

			char va_buffer[0x400]{};

			va_list ap;
			va_start(ap, fmt);
			vsprintf_s(va_buffer, fmt, ap);
			va_end(ap);

			const auto time = static_cast<int>(std::max<std::int64_t>(0, *game::mp::svs_time - level_start_time) / 1000);

			utils::io::write_file(g_log->current.string, utils::string::va("%3i:%i%i %s",
				time / 60,
				time % 60 / 10,
				time % 60 % 10,
				va_buffer
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
			const std::string_view cmd = params[0];
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
				params[0],
				client.guid,
				client_num,
				client.name,
				message.data());
		}

		int get_client_num(const game::mp::gentity_s* ent)
		{
			if (!ent || !ent->client)
			{
				return -1;
			}

			const auto num = static_cast<int>(ent - game::mp::g_entities.get());
			return num >= 0 && num < *game::sv_maxclients ? num : -1;
		}

		const char* get_team_name(const game::mp::gentity_s* ent)
		{
			switch (ent->client->team)
			{
			case 1:
				return "axis";
			case 2:
				return "allies";
			case 5:
				return "spectator";
			default:
				return "";
			}
		}

		void log_damage_event(const char tag, const game::mp::gentity_s* self, const game::mp::gentity_s* attacker,
			const int damage, const unsigned int mod, const game::Weapon* weapon, const bool is_alternate,
			const std::uint8_t hit_loc)
		{
			const auto self_num = get_client_num(self);
			const auto* clients = *game::mp::svs_clients;
			if (self_num < 0 || !clients)
			{
				return;
			}

			auto attacker_num = -1;
			const char* attacker_guid = "";
			const char* attacker_name = "";
			const char* attacker_team = "world";

			if (const auto num = get_client_num(attacker); num >= 0)
			{
				attacker_num = num;
				attacker_guid = clients[num].guid;
				attacker_name = clients[num].name;
				attacker_team = get_team_name(attacker);
			}

			char weapon_name[1024]{};
			game::mp::BG_GetWeaponNameComplete(weapon, is_alternate, weapon_name, sizeof(weapon_name));

			const auto* mod_name = mod < mod_count ? game::SL_ConvertToString(*game::mp::modNames[mod]) : "badMOD";

			g_log_printf("%c;%s;%i;%s;%s;%s;%i;%s;%s;%s;%i;%s;%s\n", tag,
				clients[self_num].guid, self_num, get_team_name(self), clients[self_num].name,
				attacker_guid, attacker_num, attacker_team, attacker_name,
				weapon_name, damage, mod_name,
				game::SL_ConvertToString(game::mp::G_GetHitLocationString(hit_loc)));
		}

		void scr_player_damage_stub(game::mp::gentity_s* self, game::mp::gentity_s* inflictor, game::mp::gentity_s* attacker,
			const int damage, const int dflags, const unsigned int mod, const game::Weapon* weapon, const bool is_alternate,
			const float* point, const float* dir, const std::uint8_t hit_loc, const int time_offset)
		{
			log_damage_event('D', self, attacker, damage, mod, weapon, is_alternate, hit_loc);
			scr_player_damage_hook.invoke<void>(self, inflictor, attacker, damage, dflags, mod, weapon, is_alternate,
				point, dir, hit_loc, time_offset);
		}

		void scr_player_killed_stub(game::mp::gentity_s* self, game::mp::gentity_s* inflictor, game::mp::gentity_s* attacker,
			const int damage, const unsigned int mod, const game::Weapon* weapon, const bool is_alternate, const float* dir,
			const std::uint8_t hit_loc, const int time_offset, const int death_anim_duration)
		{
			log_damage_event('K', self, attacker, damage, mod, weapon, is_alternate, hit_loc);
			scr_player_killed_hook.invoke<void>(self, inflictor, attacker, damage, mod, weapon, is_alternate, dir,
				hit_loc, time_offset, death_anim_duration);
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

			scripting::on_init([]
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

			scr_player_damage_hook.create(game::mp::Scr_PlayerDamage, scr_player_damage_stub);
			scr_player_killed_hook.create(game::mp::Scr_PlayerKilled, scr_player_killed_stub);
		}
	};
}

REGISTER_COMPONENT(logfile::component)
