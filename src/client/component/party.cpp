#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "scheduler.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include "console/console.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace party
{
	namespace
	{
		constexpr int max_clients = 18;

		void set_client_team(const int client_num, const int team)
		{
			if (client_num < 0 || client_num >= max_clients)
			{
				console::error("invalid client num %d\n", client_num);
				return;
			}

			auto& ent = game::mp::g_entities[client_num];

			if (!ent.client)
			{
				console::error("client %d has no gclient\n", client_num);
				return;
			}

			const auto old_team = ent.client->team;
			ent.client->team = team;

			console::info("set client %d team %d -> %d\n", client_num, old_team, team);
		}

		std::string get_gametype_or_default(const command::params& params)
		{
			if (params.size() >= 3)
			{
				return params[2];
			}

			const auto* g_gametype = game::Dvar_FindMalleableVar("1924"); // g_gametype
			return g_gametype ? g_gametype->current.string : "dm";
		}

		bool get_map_index(const std::string& map_name, int& map_index)
		{
			map_index = game::UI_GetListIndexFromMapName(map_name.data());

			if (map_index < 0)
			{
				console::error("Map '%s' not found in UI list.\n", map_name.data());
				return false;
			}

			return true;
		}

		void set_party_map_settings(const std::string& map_name, const std::string& gametype)
		{
			game::UI_SetMap(map_name.data(), gametype.data());

			// This fixes UI elements, scoreboard for example.
			const auto party = game::Lobby_GetPartyData(0);
			game::Party_SetMapName(party, map_name.data());
			game::Party_SetGameType(party, gametype.data());
		}

		void set_map_dvars(const std::string& map_name, const std::string& gametype, const int map_index, const bool set_gametype)
		{
			if (set_gametype)
			{
				game::Dvar_SetStringByName("1924", gametype.data()); // g_gametype
			}

			game::Dvar_SetStringByName("1673", map_name.data()); // mapname
			game::Dvar_SetIntByName("864", map_index);           // ui_mapname
		}

		void start_server()
		{
			*game::mp::sv_migrate = 0;

			const auto* args = "StartServer";
			game::UI_RunMenuScript(0, &args);
		}

		void assign_team_when_ready()
		{
			scheduler::loop([]()
			{
				if (!game::is_server_running())
				{
					return scheduler::cond_continue;
				}

				auto& ent = game::mp::g_entities[0];

				if (!ent.client)
				{
					return scheduler::cond_continue;
				}

				if (ent.client->team == 0)
				{
					ent.client->team = 2; // TEAM_ALLIES
				}

				return scheduler::cond_end;

			}, scheduler::pipeline::server, 500ms);
		}

		void start_map(const command::params& params)
		{
			if (params.size() < 2)
			{
				console::info("usage: map <mapname> [gametype]: loads a map with an optional gametype\n");
				return;
			}

			if (game::is_server_running())
			{
				// TODO: implement mid match map changing.
				return;
			}

			const std::string map_name = params[1];
			const std::string gametype = get_gametype_or_default(params);
			const bool has_gametype = params.size() >= 3;

			int map_index = 0;
			if (!get_map_index(map_name, map_index))
			{
				return;
			}

			console::info("Starting map '%s' index %d gametype '%s'\n",
				map_name.data(),
				map_index,
				gametype.data());

			set_party_map_settings(map_name, gametype);
			set_map_dvars(map_name, gametype, map_index, has_gametype);
			start_server();

			// Dirty hack but this needs GSC/LUA fixes.
			// Probably breaks when networking gets implemented.
			assign_team_when_ready();
		}

		void set_team_command(const command::params& params)
		{
			if (params.size() < 2)
			{
				console::info("usage: setteam <team> [clientnum]\n");
				console::info("teams: 0 = none, 1 = axis, 2 = allies, 3 = spectator\n");
				return;
			}

			const auto team = std::atoi(params[1]);
			const auto client_num = params.size() >= 3
				? std::atoi(params[2])
				: 0;

			set_client_team(client_num, team);
		}
	}

	int get_connected_client_count()
	{
		int count = 0;
		auto* clients = *game::mp::svs_clients;

		if (!clients)
		{
			return 0;
		}

		for (int i = 0; i < *game::mp::sv_maxclients; ++i)
		{
			const auto& client = clients[i];

			if (client.state != 0)
			{
				++count;
			}
		}

		return count;
	}

	int get_available_match_slots()
	{
		return std::max(0, max_clients - get_connected_client_count());
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			command::add("map_restart", []()
			{
				*game::sv_map_restart = 1;
				*game::sv_loadScripts = 1;
				*game::mp::sv_migrate = 0;

				game::mp::SV_MapRestart(*game::mp::sv_migrate, *game::sv_loadScripts);
			});

			command::add("fast_restart", []()
			{
				game::SV_FastRestart_f();
			});

			command::add("map", [](const command::params& params)
			{
				start_map(params);
			});

			// This is a temporary command until we have proper team management in place.
			command::add("setTeam", [](const command::params& params)
			{
				set_team_command(params);
			});
		}
	};
}

REGISTER_COMPONENT(party::component)
