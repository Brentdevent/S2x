#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "scheduler.hpp"
#include "network.hpp"

#include "game/dvars.hpp"

#include "console/console.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>
#include <utils/cryptography.hpp>
#include <utils/info_string.hpp>

namespace party
{
	namespace
	{
		utils::hook::detour cl_connect_hook;

		// Technically max clients is 48, but needs more patches to work properly
		constexpr int total_max_clients = 18;

		struct connect_state_t
		{
			game::netadr_s host{};
			std::string challenge{};
			bool host_defined{ false };
		};

		connect_state_t connect_state{};

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

		std::string get_current_mapname()
		{
			const auto* dvar = game::Dvar_FindMalleableVar("1673"); // mapname
			if (dvar && dvar->current.string)
			{
				return dvar->current.string;
			}

			return {};
		}

		std::string get_current_gametype()
		{
			const auto* dvar = game::Dvar_FindMalleableVar("1924"); // g_gametype
			if (dvar && dvar->current.string)
			{
				return dvar->current.string;
			}

			return "dm";
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

		void start_server()
		{
			*game::sv_migrate = 0;

			const auto* args = "StartServer";
			game::UI_RunMenuScript(0, &args);
		}

		bool validate_map_and_gametype(const std::string& mapname, const std::string& gametype)
		{
			if (mapname.empty())
			{
				console::error("Connection failed: invalid map.\n");
				return false;
			}

			if (gametype.empty())
			{
				console::error("Connection failed: invalid gametype.\n");
				return false;
			}

			int map_index = 0;
			if (!get_map_index(mapname, map_index))
			{
				console::error("Connection failed: map '%s' is not available locally.\n", mapname.data());
				return false;
			}

			return true;
		}

		void connect_to_server(const game::netadr_s& target, const std::string& mapname, const std::string& gametype, const int max_clients)
		{
			if (!validate_map_and_gametype(mapname, gametype))
			{
				return;
			}

			int map_index = 0;
			if (!get_map_index(mapname, map_index))
			{
				return;
			}

			const auto clamped_max_clients = std::clamp(max_clients, 2, total_max_clients);
			game::Dvar_SetIntByName("2299", clamped_max_clients); // sv_maxclients

			console::info(
				"Connecting to %s on map '%s' gametype '%s'\n",
				network::net_adr_to_string(target),
				mapname.data(),
				gametype.data()
			);

			set_party_map_settings(mapname, gametype);
			set_map_dvars(mapname, gametype, map_index, true);

			char session_info[0x100]{};
			auto target_copy = target;

			game::CL_ConnectAndPreloadMap(
				0,
				session_info,
				&target_copy,
				mapname.data(),
				gametype.data()
			);
		}

		void connect(const game::netadr_s& target)
		{
			if (target.type <= game::NA_BAD)
			{
				console::error("Cannot connect to bad address.\n");
				return;
			}

			if (game::virtualLobby_Loaded)
			{
				game::CL_VirtualLobbyShutdown(0, 0);
			}

			connect_state.host = target;
			connect_state.challenge = utils::cryptography::random::get_challenge();
			connect_state.host_defined = true;

			console::info(
				"Querying server %s...\n",
				network::net_adr_to_string(connect_state.host)
			);

			network::send(connect_state.host, "s2x_getInfo", connect_state.challenge);
		}

		void reconnect()
		{
			if (!connect_state.host_defined)
			{
				console::info("Cannot reconnect: no previous server.\n");
				return;
			}

			connect(connect_state.host);
		}

		void cl_connect_stub()
		{
			const auto argc = game::Cmd_Argc();

			if (argc == 2 && !game::is_local_play())
			{
				const auto* address_string = game::Cmd_Argv(1);

				game::netadr_s target{};
				if (!game::NET_StringToAdr(address_string, &target))
				{
					console::error("Invalid address: %s\n", address_string);
					return;
				}

				target.localNetID = game::NS_SERVER;
				target.addrHandleIndex = 0;

				connect(target);
				return;
			}

			cl_connect_hook.invoke<void>();
		}

		void set_client_team(const int client_num, const int team)
		{
			if (client_num < 0 || client_num >= total_max_clients)
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

	game::netadr_s& get_target()
	{
		return connect_state.host;
	}

	int get_connected_client_count()
	{
		int count = 0;
		auto* clients = *game::mp::svs_clients;

		if (!clients)
		{
			return 0;
		}

		for (int i = 0; i < *game::sv_maxclients; ++i)
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
		return std::max(0, total_max_clients - get_connected_client_count());
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			cl_connect_hook.create(game::CL_Connect, cl_connect_stub);

			command::add("map_restart", []()
			{
				*game::sv_map_restart = 1;
				*game::sv_loadScripts = 1;
				*game::sv_migrate = 0;

				game::mp::SV_MapRestart(*game::sv_migrate, *game::sv_loadScripts);
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

			command::add("reconnect", [](const command::params&)
			{
				reconnect();
			});

			network::on("s2x_getInfo", [](const game::netadr_s& from, const std::string_view& data)
			{
				utils::info_string info{};

				const auto mapname = get_current_mapname();
				const auto gametype = get_current_gametype();

				info.set("challenge", std::string{ data });
				info.set("gamename", "S2");
				info.set("mapname", mapname);
				info.set("gametype", gametype);
				info.set("clients", std::to_string(get_connected_client_count()));
				//info.set("botcount", "0");
				info.set("sv_maxclients", std::to_string(*game::sv_maxclients));
				info.set("sv_running", game::is_server_running() ? "1" : "0");
				info.set("protocol", std::to_string(PROTOCOL));

				network::send(from, "s2x_infoResponse", info.build(), '\n');
			});

			network::on("s2x_infoResponse", [](const game::netadr_s& from, const std::string_view& data)
			{
				console::info(
					"[party] getInfo from %s challenge=%.*s\n",
					network::net_adr_to_string(from),
					static_cast<int>(data.size()),
					data.data()
				);

				const utils::info_string info{ std::string{data} };

				const auto challenge = info.get("challenge");
				if (challenge != connect_state.challenge)
				{
					// Not our connect query, or stale response.
					return;
				}

				const auto protocol = std::atoi(info.get("protocol").data());
				if (protocol != PROTOCOL)
				{
					console::error("Connection failed: invalid protocol %i.\n", protocol);
					return;
				}

				const auto gamename = info.get("gamename");
				if (gamename != "S2")
				{
					console::error("Connection failed: invalid gamename '%s'.\n", gamename.data());
					return;
				}

				const auto sv_running = info.get("sv_running");
				if (sv_running != "1")
				{
					console::error("Connection failed: server is not running.\n");
					return;
				}

				const auto mapname = info.get("mapname");
				const auto gametype = info.get("gametype");

				if (!validate_map_and_gametype(mapname, gametype))
				{
					return;
				}

				const auto max_clients = std::atoi(info.get("sv_maxclients").data());
				const auto server_max_clients = max_clients > 0 ? max_clients : total_max_clients;

				console::info(
					"Server response from %s: map='%s' gametype='%s' clients=%s/%s\n",
					network::net_adr_to_string(from),
					mapname.data(),
					gametype.data(),
					info.get("clients").data(),
					info.get("sv_maxclients").data()
				);

				auto target = from;

				scheduler::once([target, mapname, gametype, server_max_clients]()
				{
					connect_to_server(target, mapname, gametype, server_max_clients);
				}, scheduler::pipeline::main);
			});
		}
	};
}

REGISTER_COMPONENT(party::component)
