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
	int get_connected_client_count();

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
		bool dedicated_map_transition_started{};
		bool listen_map_transition_in_progress{};

		bool get_map_index(const std::string& map_name, int& map_index)
		{
			map_index = game::UI_GetListIndexFromMapName(map_name.data());

			if (map_index <= 0)
			{
				console::error("Map '%s' not found in UI list.\n", map_name.data());
				return false;
			}

			return true;
		}

		void set_max_agents(const uint8_t amount)
		{
			// Part of handleGo sets max_agents in the party data.
			const auto lobby_ref = utils::hook::invoke<void*>(0x470D30_g, 0);
			const auto party = utils::hook::invoke<std::uintptr_t>(0x470F20_g, lobby_ref);

			if (!party)
			{
				return;
			}

			const auto v27 = utils::hook::invoke<std::uintptr_t>(0x47D290_g, party);
			const auto v28 = utils::hook::invoke<unsigned int>(0x470D50_g, v27);
			const auto settings = utils::hook::invoke<std::uintptr_t>(0x924650_g, v28);

			if (!settings)
			{
				return;
			}

			*reinterpret_cast<std::uint8_t*>(settings + 0x31) = amount;
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
				game::Dvar_SetStringByName("g_gametype", gametype.data());
			}

			game::Dvar_SetStringByName("mapname", map_name.data());
			game::Dvar_SetIntByName("ui_mapname", map_index);
		}

		std::string get_current_mapname()
		{
			const auto* dvar = game::Dvar_FindMalleableVar("mapname");
			if (dvar && dvar->current.string)
			{
				return dvar->current.string;
			}

			return {};
		}

		std::string get_current_gametype()
		{
			const auto* dvar = game::Dvar_FindMalleableVar("g_gametype");
			if (dvar && dvar->current.string)
			{
				return dvar->current.string;
			}

			return "dm";
		}

		std::string get_current_hostname()
		{
			const auto* dvar = game::Dvar_FindMalleableVar("sv_hostname");
			if (dvar && dvar->current.string && dvar->current.string[0])
			{
				return dvar->current.string;
			}

			return "S2x Server";
		}

		std::string get_gametype_or_default(const command::params& params)
		{
			if (params.size() >= 3)
			{
				return params[2];
			}

			const auto* g_gametype = game::Dvar_FindMalleableVar("g_gametype");
			return g_gametype ? g_gametype->current.string : "dm";
		}

		bool is_valid_gametype(const std::string& gametype)
		{
			// The stock helper returns its input pointer when the gametype is absent
			// from maps/mp/gametypes/_gametypes.txt.
			return utils::hook::invoke<const char*>(0x6500E0_g, gametype.data()) != gametype.data();
		}

		void start_server_ui()
		{
			*game::sv_migrate = 0;

			const auto* args = "StartServer";
			game::UI_RunMenuScript(0, &args);
		}

		bool com_sv_running()
		{
			const auto* dvar = game::Dvar_FindMalleableVar("com_sv_running");
			return dvar && dvar->current.enabled;
		}

		void log_dedicated_start_state(const char* stage)
		{
			console::info(
				"Dedicated direct map start state [%s]: virtualLobby_Loaded=%d, com_sv_running=%d, "
				"SV_Loaded=%d, frontend_state=%d, databaseCompletedEvent2=%d, "
				"g_skipReadAlwaysLoadedAssets=%d.\n",
				stage,
				game::virtual_lobby_loaded(),
				com_sv_running(),
				game::SV_Loaded(),
				*game::frontend_state,
				*game::databaseCompletedEvent2,
				*game::g_skipReadAlwaysLoadedAssets
			);
		}

		void monitor_dedicated_map_start(const std::string& map_name)
		{
			const auto start_time = std::chrono::steady_clock::now();

			scheduler::schedule([map_name, start_time]()
			{
				if (com_sv_running() && game::SV_Loaded() && !game::virtual_lobby_loaded())
				{
					log_dedicated_start_state("map running");
					console::info(
						"Dedicated direct map start: deferred UI_Map executed and map '%s' is running.\n",
						map_name.data()
					);

					return scheduler::cond_end;
				}

				if (std::chrono::steady_clock::now() - start_time >= 30s)
				{
					log_dedicated_start_state("map start timeout");
					console::error(
						"Dedicated direct map start: map '%s' did not reach com_sv_running. "
						"The legacy path was not started.\n",
						map_name.data()
					);

					return scheduler::cond_end;
				}

				return scheduler::cond_continue;
			}, scheduler::pipeline::main);
		}

		void start_dedicated_server_direct(const std::string& map_name)
		{
			constexpr auto local_client = 0;

			log_dedicated_start_state("before native transition");
			console::info(
				"Dedicated direct map start: requesting the native frontend/hub transition for map '%s'.\n",
				map_name.data()
			);

			// UI_RunMenuScript's StartServer branch only parses the script name and calls
			// UI_StartServer(localClientNum). Calling it directly preserves the working native path:
			// virtual-lobby shutdown, render/cinematic synchronization, frontend_state = 0,
			// party gametype refresh, and deferred UI_Map queueing.
			game::UI_StartServer(local_client);

			// UI_Map runs from the engine command-function queue on a later Cbuf execution.
			// This lets command text queued by perform_game_init() drain after the map command
			// returns. UI_Map then sets g_skipReadAlwaysLoadedAssets to 1, synchronizes the DB, and calls
			// SV_StartMapForParty(0, activePartyMap, false, false). SV_SpawnServer then requests
			// the 0x88 level-zone transition; the DB logger reports each hub zone actually unloaded.
			log_dedicated_start_state("UI_Map queued");
			console::info("Dedicated direct map start: waiting for the deferred native map start.\n");
			monitor_dedicated_map_start(map_name);
		}

		void start_server(const std::string& map_name)
		{
			if (!game::environment::is_dedi())
			{
				start_server_ui();
				return;
			}

			start_dedicated_server_direct(map_name);
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

		void perform_game_init()
		{
			if (!game::environment::is_zombies())
			{
				game::Cbuf_AddText(0, "exec default_xboxlive.cfg\n");

				if (!game::environment::is_dedi())
				{
					set_max_agents(6);
				}
			}
		}

		void restart_map()
		{
			*game::sv_map_restart = 1;
			*game::sv_loadScripts = 1;
			*game::sv_migrate = 0;

			game::mp::SV_MapRestart(*game::sv_migrate, *game::sv_loadScripts);
		}

		void start_validated_map(const std::string& map_name, const std::string& gametype,
			const int map_index, const bool set_gametype)
		{
			perform_game_init();
			set_party_map_settings(map_name, gametype);
			set_map_dvars(map_name, gametype, map_index, set_gametype);
			start_server(map_name);
		}

		void start_listen_map_transition(const std::string& map_name, const std::string& gametype,
			const int map_index, const bool set_gametype)
		{
			listen_map_transition_in_progress = true;
			const auto start_time = std::chrono::steady_clock::now();

			const auto* args = "Leave";
			game::UI_RunMenuScript(0, &args);

			// S2's Leave menu script queues "disconnect\n" in the command buffer.
			// Wait for that command and its UI transition to complete on later main frames.
			scheduler::schedule([map_name, gametype, map_index, set_gametype, start_time,
				map_start_requested = false]() mutable
			{
				if (!map_start_requested && !com_sv_running() && !game::SV_Loaded()
					&& *game::frontend_state != 0)
				{
					map_start_requested = true;
					start_validated_map(map_name, gametype, map_index, set_gametype);
				}

				if (map_start_requested && game::is_server_running()
					&& get_current_mapname() == map_name)
				{
					listen_map_transition_in_progress = false;
					return scheduler::cond_end;
				}

				if (std::chrono::steady_clock::now() - start_time >= 30s)
				{
					console::error("Listen map transition to '%s' timed out.\n", map_name.data());
					listen_map_transition_in_progress = false;
					return scheduler::cond_end;
				}

				return scheduler::cond_continue;
			}, scheduler::pipeline::main);
		}

		void connect_to_server(const game::netadr_s& target, const std::string& mapname,
			const std::string& gametype, const int max_clients)
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
			game::Dvar_SetIntByName("sv_maxclients", clamped_max_clients);

			console::info(
				"Connecting to %s on map '%s' gametype '%s'\n",
				network::net_adr_to_string(target),
				mapname.data(),
				gametype.data()
			);

			set_party_map_settings(mapname, gametype);
			set_map_dvars(mapname, gametype, map_index, true);

			perform_game_init();

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

			if (!game::virtual_lobby_loaded())
			{
				console::error("Cannot connect: virtual lobby is not loaded.\n");
				return;
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

		void start_map(const command::params& params)
		{
			if (params.size() < 2)
			{
				console::info("usage: map <mapname> [gametype]: loads a map with an optional gametype\n");
				return;
			}

			if (listen_map_transition_in_progress)
			{
				console::error("A map transition is already in progress.\n");
				return;
			}

			const auto map_name = utils::string::to_lower(params[1]);
			const std::string gametype = get_gametype_or_default(params);
			const bool has_gametype = params.size() >= 3;
			if (!validate_map_and_gametype(map_name, gametype))
			{
				return;
			}

			if (has_gametype && !is_valid_gametype(gametype))
			{
				console::error("Gametype '%s' is not available locally.\n", gametype.data());
				return;
			}

			int map_index = 0;
			if (!get_map_index(map_name, map_index))
			{
				return;
			}

			const auto server_running = game::is_server_running();
			if (server_running && get_current_mapname() == map_name)
			{
				restart_map();
				return;
			}

			const auto is_dedicated = game::environment::is_dedi();
			if (is_dedicated && server_running)
			{
				console::info("Live dedicated map changing is not implemented yet.\n");
				return;
			}

			if (!is_dedicated && server_running)
			{
				start_listen_map_transition(map_name, gametype, map_index, has_gametype);
				return;
			}

			if (is_dedicated)
			{
				if (!game::virtual_lobby_loaded())
				{
					console::info("Ignoring early dedicated map command until the virtual lobby is loaded.\n");
					return;
				}

				if (dedicated_map_transition_started)
				{
					console::info("Ignoring duplicate dedicated map transition for '%s'.\n", map_name.data());
					return;
				}

				dedicated_map_transition_started = true;
			}

			console::info("Starting map '%s' index %d gametype '%s'\n",
				map_name.data(),
				map_index,
				gametype.data());

			start_validated_map(map_name, gametype, map_index, has_gametype);
		}

		void send_info_response(const game::netadr_s& from, const std::string_view& data, const std::string& response_command)
		{
			utils::info_string info{};

			const auto mapname = get_current_mapname();
			const auto gametype = get_current_gametype();
			const auto hostname = get_current_hostname();

			info.set("challenge", std::string{ data });
			info.set("gamename", "S2");
			info.set("hostname", hostname);
			info.set("sv_hostname", hostname);
			info.set("mapname", mapname);
			info.set("gametype", gametype);
			info.set("clients", std::to_string(get_connected_client_count()));
			info.set("bots", "0");
			info.set("sv_maxclients", std::to_string(*game::sv_maxclients));
			info.set("sv_running", game::is_server_running() ? "1" : "0");
			info.set("protocol", std::to_string(PROTOCOL));

			auto payload = info.build();
			payload.append("\\s2x\\1");

			network::send(from, response_command, payload, '\n');
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
			// Fixes team setting + enables team switching
			game::Dvar_RegisterBool("3193", true, game::DVAR_FLAG_READ);

			cl_connect_hook.create(game::CL_Connect, cl_connect_stub);

			command::add("map_restart", []()
			{
				restart_map();
			});

			command::add("fast_restart", []()
			{
				game::SV_FastRestart_f();
			});

			command::add("map", [](const command::params& params)
			{
				start_map(params);
			});

			command::add("reconnect", [](const command::params&)
			{
				reconnect();
			});

			network::on("s2x_getInfo", [](const game::netadr_s& from, const std::string_view& data)
			{
				send_info_response(from, data, "s2x_infoResponse");
			});

			/*network::on("getinfo", [](const game::netadr_s& from, const std::string_view& data)
			{
				send_info_response(from, data, "infoResponse");
			});*/

			network::on("s2x_infoResponse", [](const game::netadr_s& from, const std::string_view& data)
			{
				const utils::info_string info{ std::string{data} };

				const auto challenge = info.get("challenge");
				if (challenge != connect_state.challenge)
				{
					// Not our connect query, or stale response.
					return;
				}

				console::info(
					"[party] getInfo from %s challenge=%.*s\n",
					network::net_adr_to_string(from),
					static_cast<int>(data.size()),
					data.data()
				);

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
					if (game::virtual_lobby_loaded())
					{
						console::info("Leaving virtual lobby before direct connection.\n");
						game::CL_VirtualLobbyShutdown(0, 0);
					}

					scheduler::once([target, mapname, gametype, server_max_clients]()
					{
						connect_to_server(target, mapname, gametype, server_max_clients);
					}, scheduler::pipeline::main);
				}, scheduler::pipeline::main);
			});
		}
	};
}

REGISTER_COMPONENT(party::component)
