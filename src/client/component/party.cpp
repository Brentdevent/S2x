#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "party.hpp"
#include "dedicated_party.hpp"
#include "command.hpp"
#include "scheduler.hpp"
#include "network.hpp"

#include "game/dvars.hpp"
#include "game/ui_scripting/execution.hpp"

#include "ui_scripting.hpp"

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
		utils::hook::detour cl_connect_and_preload_map_hook;
		utils::hook::detour party_atomic_setup_potential_host_hook;
		utils::hook::detour party_client_handle_go_hook;
		utils::hook::detour party_client_process_party_state_hook;

		// Technically max clients is 48, but needs more patches to work properly
		constexpr int total_max_clients = 18;

		struct connect_state_t
		{
			game::netadr_s host{};
			std::string challenge{};
			bool host_defined{ false };
		};

		connect_state_t connect_state{};
		bool listen_map_transition_in_progress{};

		struct hosted_party_join_state_t
		{
			bool active{};
			game::netadr_s target{};
			std::string session_id{};
			std::string map_name{};
			std::string gametype{};
		};

		hosted_party_join_state_t hosted_party_join_state{};

		struct hosted_dedicated_party_state_t
		{
			game::netadr_s target{};
			std::string session_id{};
			std::string map_name{};
			std::string gametype{};
			std::string sync_challenge{};
			bool sync_after_next_go{};
		};

		hosted_dedicated_party_state_t hosted_dedicated_party_state{};
		bool hosted_dedicated_go_in_progress{};

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
			const auto lobby_ref = game::Lobby_GetLocalClientData(0);
			const auto party = reinterpret_cast<std::uintptr_t>(
				game::Lobby_GetPartyDataFromLocalClient(lobby_ref));

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
			if (party)
			{
				game::Party_SetMapName(party, map_name.data());
				game::Party_SetGameType(party, gametype.data());
			}
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

		bool is_session_hex_string(const std::string& value, const std::size_t expected_size)
		{
			return value.size() == expected_size
				&& std::all_of(value.begin(), value.end(), [](const unsigned char character)
				{
					return std::isxdigit(character) != 0;
				});
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
			start_server_ui();
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

			hosted_dedicated_party_state = {};
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
			const auto is_dedicated = game::environment::is_dedi();
			if (is_dedicated && (server_running || dedicated_party::is_active()))
			{
				if (!dedicated_party::set_next_match(map_name, gametype, map_index))
				{
					console::error("Dedicated party lifecycle is not active.\n");
				}

				return;
			}

			if (server_running && get_current_mapname() == map_name)
			{
				restart_map();
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

				dedicated_party::start();
				return;
			}

			console::info("Starting map '%s' index %d gametype '%s'\n",
				map_name.data(),
				map_index,
				gametype.data());

			start_validated_map(map_name, gametype, map_index, has_gametype);
		}

		bool pending_hosted_party_join_matches(const void* session_info)
		{
			if (!hosted_party_join_state.active || !session_info)
			{
				return false;
			}

			std::array<char, 17> session_id{};
			game::Session_IdToString(
				*reinterpret_cast<const std::uint64_t*>(session_info), session_id.data());
			return hosted_party_join_state.session_id == session_id.data();
		}

		bool is_hosted_dedicated_party_address(const game::netadr_s* address)
		{
			return address && !hosted_dedicated_party_state.session_id.empty()
				&& game::NET_CompareAdr(address, &hosted_dedicated_party_state.target);
		}

		bool update_hosted_dedicated_party_match(const std::string_view map_name,
			const std::string_view gametype, const bool apply_settings)
		{
			const std::string map_name_value{ map_name };
			const std::string gametype_value{ gametype };
			int map_index = 0;
			if (map_name_value.empty() || gametype_value.empty()
				|| !get_map_index(map_name_value, map_index)
				|| !is_valid_gametype(gametype_value))
			{
				return false;
			}

			const auto changed = hosted_dedicated_party_state.map_name != map_name_value
				|| hosted_dedicated_party_state.gametype != gametype_value;
			hosted_dedicated_party_state.map_name = map_name_value;
			hosted_dedicated_party_state.gametype = gametype_value;

			if (apply_settings)
			{
				set_party_map_settings(map_name_value, gametype_value);
				set_map_dvars(map_name_value, gametype_value, map_index, true);
			}

			if (changed)
			{
				console::info("Hosted dedicated lobby: match updated to %s %s.\n",
					map_name_value.data(), gametype_value.data());
			}

			return true;
		}

		std::string get_hosted_dedicated_party_gametype()
		{
			if (game::environment::is_dedi())
			{
				return dedicated_party::get_current_gametype();
			}

			if (hosted_dedicated_party_state.gametype.empty())
			{
				return {};
			}

			return hosted_dedicated_party_state.gametype;
		}

		void request_hosted_dedicated_party_sync()
		{
			if (!hosted_dedicated_party_state.sync_after_next_go
				|| !hosted_dedicated_party_state.sync_challenge.empty())
			{
				return;
			}

			hosted_dedicated_party_state.sync_after_next_go = false;
			hosted_dedicated_party_state.sync_challenge =
				utils::cryptography::random::get_challenge();
			network::send(hosted_dedicated_party_state.target, "s2x_getInfo",
				hosted_dedicated_party_state.sync_challenge);
		}

		void party_client_process_party_state_stub(void* party_data,
			std::uint32_t* active_client, game::netadr_s* from)
		{
			party_client_process_party_state_hook.invoke<void>(
				party_data, active_client, from);

			auto* game_lobby = game::Lobby_GetPartyData(0);
			if (!is_hosted_dedicated_party_address(from)
				|| party_data != game_lobby)
			{
				return;
			}

			// Public partystate applies its playlist rules after parsing and can replace
			// the dedicated host's free-form map/gametype with a local default. Keep the
			// native party state authoritative for UI and gameplay team initialization.
			if (!hosted_dedicated_party_state.map_name.empty()
				&& !hosted_dedicated_party_state.gametype.empty())
			{
				update_hosted_dedicated_party_match(
					hosted_dedicated_party_state.map_name,
					hosted_dedicated_party_state.gametype,
					true);
			}

			if (game::virtual_lobby_loaded())
			{
				// Refresh once after a match so the next rotation selection is learned.
				request_hosted_dedicated_party_sync();
			}
		}

		std::int64_t party_client_handle_go_stub(void* party_data, void* command_data,
			game::netadr_s* from, game::msg_t* message)
		{
			std::string map_name_value{};
			std::string gametype_value{};
			if (is_hosted_dedicated_party_address(from) && game::Cmd_Argc() > 6)
			{
				// PartyClient_HandleGo passes argv[5] and argv[6] to
				// CL_ConnectAndPreloadMap as the map and gametype respectively.
				const auto* map_name = game::Cmd_Argv(5);
				const auto* gametype = game::Cmd_Argv(6);
				if (map_name && gametype
					&& update_hosted_dedicated_party_match(map_name, gametype, false))
				{
					map_name_value = map_name;
					gametype_value = gametype;
					hosted_dedicated_party_state.sync_after_next_go = true;
				}
			}

			hosted_dedicated_go_in_progress = !map_name_value.empty();
			const auto result = party_client_handle_go_hook.invoke<std::int64_t>(
				party_data, command_data, from, message);
			hosted_dedicated_go_in_progress = false;

			if (!map_name_value.empty())
			{
				update_hosted_dedicated_party_match(
					map_name_value, gametype_value, true);
			}

			return result;
		}

		void cl_connect_and_preload_map_stub(const int local_client_num, void* session_info,
			game::netadr_s* target, const char* map_name, const char* gametype)
		{
			if (hosted_dedicated_go_in_progress && map_name && gametype)
			{
				// Public PartyClient_HandleGo runs Playlist_RunRules before this call.
				// Restore the map/gametype carried by the go command at the last native
				// boundary before client gameplay memory and UI state are selected.
				update_hosted_dedicated_party_match(map_name, gametype, true);
			}

			cl_connect_and_preload_map_hook.invoke<void>(
				local_client_num, session_info, target, map_name, gametype);
		}

		void install_lobby_functions()
		{
			const auto lua = ui_scripting::get_globals();
			auto lobby_value = lua.get("Lobby");

			ui_scripting::table lobby{};
			if (lobby_value.is<ui_scripting::table>())
			{
				lobby = lobby_value.as<ui_scripting::table>();
			}
			else
			{
				lua["Lobby"] = lobby;
			}

			lobby["GetDedicatedPartyGameType"] = []
			{
				return get_hosted_dedicated_party_gametype();
			};
		}

		bool party_atomic_setup_potential_host_stub(const int controller_index, const void* session_info,
			const int party_type, const int max_players, const int a5, const int a6, void* join_info)
		{
			const auto is_hosted_party_join = pending_hosted_party_join_matches(session_info);

			const auto result = party_atomic_setup_potential_host_hook.invoke<bool>(
				controller_index, session_info, party_type, max_players, a5, a6, join_info);

			if (!is_hosted_party_join)
			{
				return result;
			}

			const auto target = hosted_party_join_state.target;
			const auto session_id = hosted_party_join_state.session_id;
			const auto map_name = hosted_party_join_state.map_name;
			const auto gametype = hosted_party_join_state.gametype;
			hosted_party_join_state = {};

			if (!join_info)
			{
				return result;
			}

			if (!result)
			{
				// Raw S2x networking does not establish the secure address handle that
				// sub_827860 normally resolves. Restore the stock temporary session after
				// that failed conversion, then let the party state machine use the OOB peer.
				constexpr int online_connection_type = 46;
				const auto session_index = 4 - static_cast<int>(party_type != 0);
				auto* session = game::Session_GetData(session_index);

				if (!session)
				{
					console::error("Hosted dedicated lobby: native party session is unavailable.\n");
					return false;
				}

				utils::hook::invoke<void>(0x6FD220_g, session);
				utils::hook::invoke<void>(0x6FC830_g, session);
				if (!utils::hook::invoke<bool>(
					0x6FFD70_g, session, controller_index, online_connection_type,
					session_info, 0, max_players, a5))
				{
					console::error("Hosted dedicated lobby: native party session setup failed.\n");
					return false;
				}
			}

			constexpr std::ptrdiff_t address_valid_offset = 344;
			constexpr std::ptrdiff_t address_offset = 348;
			const auto join_info_address = reinterpret_cast<std::uintptr_t>(join_info);
			*reinterpret_cast<game::netadr_s*>(join_info_address + address_offset) = target;
			*reinterpret_cast<std::uint8_t*>(join_info_address + address_valid_offset) = 1;
			hosted_dedicated_party_state = {};
			hosted_dedicated_party_state.target = target;
			hosted_dedicated_party_state.session_id = session_id;
			hosted_dedicated_party_state.map_name = map_name;
			hosted_dedicated_party_state.gametype = gametype;

			console::info("Hosted dedicated lobby: joining through %s.\n",
				network::net_adr_to_string(target));
			return true;
		}

		bool handle_hosted_party_join(const game::netadr_s& from, const utils::info_string& info)
		{
			if (info.get("party_session") != "1")
			{
				return false;
			}

			const auto host_address = info.get("session_host");
			const auto key = info.get("session_key");
			const auto session_id = info.get("session_id");
			const auto map_name = info.get("party_mapname");
			const auto gametype = info.get("party_gametype");

			if (!is_session_hex_string(host_address, 80)
				|| !is_session_hex_string(key, 32)
				|| !is_session_hex_string(session_id, 16)
				|| !validate_map_and_gametype(map_name, gametype)
				|| !is_valid_gametype(gametype))
			{
				console::error("Connection failed: invalid hosted-party session data.\n");
				return true;
			}

			console::info("Joining hosted dedicated lobby on map '%s' gametype '%s'.\n",
				map_name.data(), gametype.data());

			auto target = from;
			target.localNetID = game::NS_SERVER;

			scheduler::once([target, host_address, key, session_id, map_name, gametype]()
			{
				// This is the seven-argument command emitted by S2's stock JoinServer menu.
				// CL_Connect parses the session descriptor and calls PartyAtomic_RequestJoin.
				hosted_party_join_state = {true, target, session_id, map_name, gametype};
				game::Cbuf_AddText(0, utils::string::va("connect %s %s %s 0 0 %s %s\n",
					host_address.data(), key.data(), session_id.data(), map_name.data(), gametype.data()));
			}, scheduler::pipeline::main);

			return true;
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

			dedicated_party::connect_info party_connect_info{};
			if (dedicated_party::get_connect_info(party_connect_info))
			{
				info.set("party_session", "1");
				info.set("session_host", party_connect_info.host_address);
				info.set("session_key", party_connect_info.key);
				info.set("session_id", party_connect_info.session_id);
				info.set("party_mapname", party_connect_info.map_name);
				info.set("party_gametype", party_connect_info.gametype);
			}

			auto payload = info.build();
			payload.append("\\s2x\\1");

			network::send(from, response_command, payload, '\n');
		}
	}

	game::netadr_s& get_target()
	{
		return connect_state.host;
	}

	bool resolve_map_index(const std::string& map_name, int& map_index)
	{
		return get_map_index(map_name, map_index);
	}

	bool validate_gametype(const std::string& gametype)
	{
		return is_valid_gametype(gametype);
	}

	void apply_map_settings(const std::string& map_name, const std::string& gametype, const int map_index)
	{
		set_party_map_settings(map_name, gametype);
		set_map_dvars(map_name, gametype, map_index, true);
	}

	std::string loaded_map_name()
	{
		return get_current_mapname();
	}

	std::string loaded_gametype()
	{
		return get_current_gametype();
	}

	bool server_running()
	{
		return com_sv_running();
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
			// Enables the stock Change Team pause-menu action in supported modes.
			game::Dvar_RegisterBool("3193", true, game::DVAR_FLAG_READ);

			cl_connect_hook.create(game::CL_Connect, cl_connect_stub);
			if (!game::environment::is_dedi())
			{
				cl_connect_and_preload_map_hook.create(
					game::CL_ConnectAndPreloadMap, cl_connect_and_preload_map_stub);
				party_atomic_setup_potential_host_hook.create(
					0x497EF0_g, party_atomic_setup_potential_host_stub);
				party_client_handle_go_hook.create(
					game::PartyClient_HandleGo, party_client_handle_go_stub);
				party_client_process_party_state_hook.create(
					game::PartyClient_ProcessPartyState, party_client_process_party_state_stub);
			}

			ui_scripting::on_start(install_lobby_functions);

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
				const auto connect_response = challenge == connect_state.challenge;
				const auto party_sync_response = is_hosted_dedicated_party_address(&from)
					&& !hosted_dedicated_party_state.sync_challenge.empty()
					&& challenge == hosted_dedicated_party_state.sync_challenge;
				if (!connect_response && !party_sync_response)
				{
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

				if (party_sync_response)
				{
					hosted_dedicated_party_state.sync_challenge.clear();
					if (info.get("party_session") != "1"
						|| info.get("session_id") != hosted_dedicated_party_state.session_id)
					{
						return;
					}

					update_hosted_dedicated_party_match(
						info.get("party_mapname"), info.get("party_gametype"), true);
					return;
				}

				console::info(
					"[party] getInfo from %s challenge=%.*s\n",
					network::net_adr_to_string(from),
					static_cast<int>(data.size()),
					data.data()
				);

				// A hosted dedicated lobby remains joinable between gameplay servers. Hand
				// its stock session descriptor to CL_Connect before applying direct-game
				// connection requirements such as sv_running.
				if (handle_hosted_party_join(from, info))
				{
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
