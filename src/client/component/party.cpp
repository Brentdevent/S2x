#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "party.hpp"
#include "dedicated_party.hpp"
#include "dedicated_party_client.hpp"
#include "command.hpp"
#include "scheduler.hpp"
#include "network.hpp"

#include "game/dvars.hpp"

#include "console/console.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/nt.hpp>
#include <utils/string.hpp>
#include <utils/cryptography.hpp>
#include <utils/info_string.hpp>

#include <charconv>

namespace party
{
	int get_connected_client_count();

	namespace
	{
		utils::hook::detour cl_connect_hook;
		utils::hook::detour disconnect_command_hook;

		struct connect_state_t
		{
			game::netadr_s host{};
			std::string challenge{};
			bool host_defined{ false };
			bool query_pending{};
			std::uint64_t attempt_id{};
		};

		connect_state_t connect_state{};

		bool listen_map_transition_in_progress{};

		bool parse_info_int(const std::string& value, const int minimum,
			const int maximum, int& result)
		{
			if (value.empty())
			{
				return false;
			}

			int parsed{};
			const auto [end, error] = std::from_chars(
				value.data(), value.data() + value.size(), parsed);
			if (error != std::errc{} || end != value.data() + value.size()
				|| parsed < minimum || parsed > maximum)
			{
				return false;
			}

			result = parsed;
			return true;
		}

		bool is_matching_ip_endpoint(const game::netadr_s& lhs, const game::netadr_s& rhs)
		{
			const auto lhs_is_ip = lhs.type == game::NA_IP || lhs.type == game::NA_BROADCAST;
			const auto rhs_is_ip = rhs.type == game::NA_IP || rhs.type == game::NA_BROADCAST;
			return lhs_is_ip && rhs_is_ip && lhs.addr == rhs.addr && lhs.port == rhs.port;
		}

		void clear_pending_connect_query()
		{
			connect_state.query_pending = false;
			connect_state.challenge.clear();
		}

		std::uint64_t invalidate_connection_attempt()
		{
			++connect_state.attempt_id;
			clear_pending_connect_query();
			dedicated_party_client::cancel_pending_connection();
			return connect_state.attempt_id;
		}

		bool zombies_fastfile_is_installed(const std::string& map_name)
		{
			const auto fastfile_name = map_name + ".ff";
			const auto game_directory = utils::nt::library{}.get_folder();
			const std::array<std::filesystem::path, 2> candidates{
				fastfile_name,
				std::filesystem::path{"zone"} / fastfile_name,
			};

			for (const auto& candidate : candidates)
			{
				// Stock installs keep map fastfiles in the game root. Standalone
				// dedicated packages commonly keep the same files in zone/.
				if (utils::io::file_exists(candidate.generic_string())
					|| utils::io::file_exists((game_directory / candidate).generic_string()))
				{
					return true;
				}
			}

			return false;
		}

		bool get_map_index(const std::string& map_name, int& map_index)
		{
			const auto& mode = game::environment::get_online_mode_info();
			const std::string_view map_name_view{ map_name };
			// The stock mode records classify Multiplayer as "mp_" and Zombies as
			// the more specific "mp_zombie_". Test the specific prefix first so
			// Zombies maps cannot be accepted by Multiplayer's generic prefix.
			const auto is_zombies_map = map_name_view.starts_with("mp_zombie_");
			const auto is_multiplayer_map = !is_zombies_map && map_name_view.starts_with("mp_");
			const auto map_mode = is_zombies_map
				? game::environment::mode::zombies
				: game::environment::mode::multiplayer;
			if ((!is_zombies_map && !is_multiplayer_map)
				|| map_mode != game::environment::get_mode())
			{
				console::error(
					"Map '%s' is not a %s map.\n",
					map_name.data(), mode.token.data());
				return false;
			}

			if (game::environment::is_zombies())
			{
				const auto safe_name = std::all_of(
					map_name.begin(), map_name.end(), [](const unsigned char character)
					{
						return std::isalnum(character) != 0 || character == '_';
					});
				if (!safe_name || !zombies_fastfile_is_installed(map_name))
				{
					console::error(
						"Zombies map '%s' is not installed locally "
						"(checked the game root and zone folder).\n",
						map_name.data());
					return false;
				}

				// The sorted UI lookup is deliberately disabled by stock Zombies.
				// Refresh the mode-selected arena catalog and use its authoritative
				// unsorted map-name lookup instead.
				game::GameInfo_UpdateArenas();
				map_index = game::GameInfo_GetIndexForMapName(map_name.data());
				if (map_index < 0)
				{
					console::error(
						"Zombies map '%s' is not present in the stock arena catalog.\n",
						map_name.data());
					return false;
				}

				return true;
			}

			// The sorted UI lookup returns zero for both a missing map and its first
			// entry. Validate membership with the unambiguous catalog lookup first.
			game::GameInfo_UpdateArenas();
			if (game::GameInfo_GetIndexForMapName(map_name.data()) < 0)
			{
				console::error(
					"Multiplayer map '%s' is not present in the stock arena catalog.\n",
					map_name.data());
				return false;
			}

			map_index = game::UI_GetListIndexFromMapName(map_name.data());
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
			if (game::environment::is_multiplayer())
			{
				game::UI_SetMap(map_name.data(), gametype.data());
			}

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
			if (game::environment::is_multiplayer())
			{
				game::Dvar_SetIntByName("ui_mapname", map_index);
			}
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
			const auto& mode = game::environment::get_online_mode_info();
			if (game::environment::is_zombies())
			{
				return std::string{ mode.default_gametype };
			}

			const auto* dvar = game::Dvar_FindMalleableVar("g_gametype");
			if (dvar && dvar->current.string)
			{
				return dvar->current.string;
			}

			return std::string{ mode.default_gametype };
		}

		std::string get_current_hostname()
		{
			const auto* dvar = game::Dvar_FindMalleableVar("sv_hostname");
			if (dvar && dvar->current.string && dvar->current.string[0])
			{
				return dvar->current.string;
			}

			return "S2x Dedicated Server";
		}

		std::string get_gametype_or_default(const command::params& params)
		{
			if (params.size() >= 3)
			{
				return params[2];
			}

			const auto& mode = game::environment::get_online_mode_info();
			if (game::environment::is_zombies())
			{
				return std::string{ mode.default_gametype };
			}

			const auto* g_gametype = game::Dvar_FindMalleableVar("g_gametype");
			return g_gametype
				? std::string{ g_gametype->current.string }
				: std::string{ mode.default_gametype };
		}

		bool is_valid_gametype(const std::string& gametype)
		{
			const auto& mode = game::environment::get_online_mode_info();
			if (game::environment::is_zombies())
			{
				return utils::string::to_lower(gametype) == mode.default_gametype;
			}

			// The stock helper returns its input pointer when the gametype is absent
			// from maps/mp/gametypes/_gametypes.txt.
			return utils::hook::invoke<const char*>(0x6500E0_g, gametype.data()) != gametype.data();
		}

		void start_server_ui()
		{
			*game::sv_migrate = 0;

			const auto* args = "StartServer";
			dedicated_party_client::reset();
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

			if (game::environment::is_zombies()
				&& !is_valid_gametype(gametype))
			{
				console::error(
					"Connection failed: Zombies servers require gametype 'zombies'.\n");
				return false;
			}

			return true;
		}

		void perform_game_init()
		{
			if (game::environment::is_multiplayer())
			{
				game::Cbuf_AddText(0, "exec default_xboxlive.cfg\n");

				if (!game::environment::is_dedicated())
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
			const std::string& gametype, const int max_clients, const std::uint64_t attempt_id)
		{
			if (!is_connection_attempt_current(attempt_id))
			{
				return;
			}

			if (!validate_map_and_gametype(mapname, gametype))
			{
				return;
			}

			int map_index = 0;
			if (!get_map_index(mapname, map_index))
			{
				return;
			}

			if (!is_connection_attempt_current(attempt_id))
			{
				return;
			}

			dedicated_party_client::commit_direct_connection();

			const auto& mode = game::environment::get_online_mode_info();
			const auto clamped_max_clients = std::clamp(
				max_clients, mode.minimum_direct_players, mode.max_players);
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

		void connect(const game::netadr_s& target, const std::uint64_t attempt_id)
		{
			if (!is_connection_attempt_current(attempt_id))
			{
				return;
			}

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
			connect_state.query_pending = true;

			console::info(
				"Querying server %s...\n",
				network::net_adr_to_string(connect_state.host)
			);

			network::send(connect_state.host, "s2x_getInfo", connect_state.challenge);

			const auto challenge = connect_state.challenge;
			scheduler::once([target, challenge, attempt_id]()
			{
				if (!is_connection_attempt_current(attempt_id)
					|| !connect_state.query_pending || connect_state.challenge != challenge
					|| !is_matching_ip_endpoint(connect_state.host, target))
				{
					return;
				}

				console::error("Connection query timed out.\n");
				clear_pending_connect_query();
			}, scheduler::pipeline::main, 10s);
		}

		void connect(const game::netadr_s& target)
		{
			connect(target, invalidate_connection_attempt());
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

			cancel_pending_connection();

			if (argc >= 8)
			{
				dedicated_party_client::reset();
			}

			cl_connect_hook.invoke<void>();
		}

		void disconnect_command_stub()
		{
			const auto preserve_hosted_party = listen_map_transition_in_progress;
			party::cancel_pending_connection();
			disconnect_command_hook.invoke<void>();

			if (!preserve_hosted_party)
			{
				dedicated_party_client::reset();
			}
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
			auto gametype = get_gametype_or_default(params);
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

			if (game::environment::is_zombies())
			{
				gametype = std::string{
					game::environment::get_online_mode_info().default_gametype };
			}

			int map_index = 0;
			if (!get_map_index(map_name, map_index))
			{
				return;
			}

			const auto server_running = game::is_server_running();
			const auto is_dedicated = game::environment::is_dedicated();
			if (is_dedicated && (server_running || dedicated_party::is_active()))
			{
				if (!dedicated_party::set_next_match(map_name, gametype, map_index))
				{
					console::error("Dedicated party lifecycle is not active.\n");
				}

				return;
			}

			if (!is_dedicated)
			{
				cancel_pending_connection();
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
				if (!dedicated_party::set_next_match(map_name, gametype, map_index))
				{
					console::error("Unable to queue the dedicated match override.\n");
					return;
				}

				if (!game::virtual_lobby_loaded())
				{
					console::info(
						"Queued dedicated map override until the virtual lobby is loaded.\n");
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

		int get_bot_count()
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
				if (client.state != 0 && (client.remoteAddress.type == game::NA_BOT || client.testClient != 0))
				{
					++count;
				}
			}

			return count;
		}

		void send_info_response(const game::netadr_s& from, const std::string_view& data, const std::string& response_command)
		{
			if (data.empty() || data.size() > 128)
			{
				return;
			}

			utils::info_string info{};

			auto mapname = get_current_mapname();
			auto gametype = get_current_gametype();
			const auto hostname = get_current_hostname();
			auto clients = get_connected_client_count();
			auto bots = get_bot_count();
			const auto& mode = game::environment::get_online_mode_info();
			auto max_clients = *game::sv_maxclients > 0
				? std::min(*game::sv_maxclients, mode.max_players)
				: mode.max_players;
			auto match_running = game::is_server_running();

			dedicated_party::connect_info party_connect_info{};
			const auto has_party_session = dedicated_party::get_connect_info(party_connect_info);
			if (has_party_session)
			{
				mapname = party_connect_info.map_name;
				gametype = party_connect_info.gametype;
				max_clients = party_connect_info.max_members;
				clients = std::clamp(party_connect_info.member_count, 0, max_clients);
				bots = std::clamp(bots, 0, clients);
				match_running = party_connect_info.match_running;
			}

			info.set("challenge", std::string{ data });
			info.set("gamename", "S2");
			info.set("mode", std::string{ mode.token });
			info.set("hostname", hostname);
			info.set("sv_hostname", hostname);
			info.set("mapname", mapname);
			info.set("gametype", gametype);
			info.set("clients", std::to_string(clients));
			info.set("bots", std::to_string(bots));
			info.set("sv_maxclients", std::to_string(max_clients));
			info.set("sv_running", match_running ? "1" : "0");
			info.set("protocol", std::to_string(PROTOCOL));
			info.set("s2x", "1");

			if (has_party_session && response_command == "s2x_infoResponse")
			{
				info.set("party_session", "1");
				info.set("session_host", party_connect_info.host_address);
				info.set("session_key", party_connect_info.key);
				info.set("session_id", party_connect_info.session_id);
				info.set("party_mapname", party_connect_info.map_name);
				info.set("party_gametype", party_connect_info.gametype);
				info.set("party_match_sequence",
					std::to_string(party_connect_info.match_sequence));
			}

			network::send(from, response_command, info.build(), '\n');
		}
	}

	game::netadr_s& get_target()
	{
		return connect_state.host;
	}

	bool is_connection_attempt_current(const std::uint64_t attempt_id)
	{
		return connect_state.attempt_id == attempt_id;
	}

	void cancel_pending_connection()
	{
		invalidate_connection_attempt();
	}

	void queue_connect(std::string address)
	{
		const auto attempt_id = invalidate_connection_attempt();

		scheduler::once([address = std::move(address), attempt_id]()
		{
			if (!is_connection_attempt_current(attempt_id))
			{
				return;
			}

			game::netadr_s target{};
			if (!game::NET_StringToAdr(address.data(), &target))
			{
				console::error("Invalid address: %s\n", address.data());
				return;
			}

			target.localNetID = game::NS_SERVER;
			target.addrHandleIndex = 0;
			connect(target, attempt_id);
		}, scheduler::pipeline::main);
	}

	void execute_internal_connect(const internal_connect_request& request)
	{
		if (!is_connection_attempt_current(request.attempt_id)
			|| !dedicated_party_client::is_pending_internal_connect(request.session_id, request.attempt_id))
		{
			return;
		}

		const std::string connect_command = utils::string::va(
			"connect %s %s %s 0 0 %s %s",
			request.host_address.data(),
			request.key.data(),
			request.session_id.data(),
			request.map_name.data(),
			request.gametype.data()
		);

		const command::params connect_params{ connect_command };
		cl_connect_hook.invoke<void>();
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
		return std::max(0,
			game::environment::get_online_mode_info().max_players - get_connected_client_count());
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_multiplayer())
			{
				// Enables the stock Change Team pause-menu action in Multiplayer.
				game::Dvar_RegisterBool("3193", true, game::DVAR_FLAG_READ);
			}

			cl_connect_hook.create(game::CL_Connect, cl_connect_stub);
			disconnect_command_hook.create(game::CL_Disconnect_f, disconnect_command_stub);

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

			network::on("getinfo", [](const game::netadr_s& from, const std::string_view& data)
			{
				send_info_response(from, data, "infoResponse");
			});

			network::on("s2x_infoResponse", [](const game::netadr_s& from, const std::string_view& data)
			{
				const utils::info_string info{ std::string{data} };
				const auto challenge = info.get("challenge");
				const auto connect_response = connect_state.query_pending
					&& connect_state.host_defined
					&& challenge == connect_state.challenge
					&& is_matching_ip_endpoint(from, connect_state.host);
				if (connect_response)
				{
					clear_pending_connect_query();
				}
				const auto attempt_id = connect_state.attempt_id;

				if (dedicated_party_client::try_handle_sync_response(from, info, challenge))
				{
					return;
				}

				if (!connect_response)
				{
					return;
				}

				int protocol{};
				if (!parse_info_int(info.get("protocol"), 0, std::numeric_limits<int>::max(), protocol)
					|| protocol != PROTOCOL)
				{
					console::error("Connection failed: invalid protocol.\n");
					return;
				}

				const auto gamename = info.get("gamename");
				if (gamename != "S2")
				{
					console::error("Connection failed: invalid gamename '%s'.\n", gamename.data());
					return;
				}

				const auto& mode = game::environment::get_online_mode_info();
				const auto server_mode = info.get("mode");
				if (server_mode != mode.token)
				{
					console::error(
						"Connection failed: server mode '%s' does not match client mode '%s'.\n",
						server_mode.empty() ? "<missing>" : server_mode.data(),
						mode.token.data());
					return;
				}

				int client_count{};
				int bot_count{};
				int max_clients{};
				if (!parse_info_int(info.get("clients"), 0, mode.max_players, client_count)
					|| !parse_info_int(info.get("bots"), 0, mode.max_players, bot_count)
					|| !parse_info_int(info.get("sv_maxclients"), 1, mode.max_players, max_clients)
					|| client_count > max_clients || bot_count > client_count)
				{
					console::error("Connection failed: invalid server player counts.\n");
					return;
				}

				console::info("[party] validated server response from %s.\n",
					network::net_adr_to_string(from));

				// A hosted dedicated lobby remains joinable between gameplay servers. Hand
				// its stock session descriptor to CL_Connect before applying direct-game
				// connection requirements such as sv_running.
				if (dedicated_party_client::try_handle_join(from, info, max_clients, attempt_id))
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

				console::info(
					"Server response from %s: map='%s' gametype='%s' clients=%i/%i\n",
					network::net_adr_to_string(from),
					mapname.data(),
					gametype.data(),
					client_count,
					max_clients
				);

				auto target = from;

				scheduler::once([target, mapname, gametype, max_clients, attempt_id]()
				{
					if (!is_connection_attempt_current(attempt_id))
					{
						return;
					}

					if (game::virtual_lobby_loaded())
					{
						console::info("Leaving virtual lobby before direct connection.\n");
						game::CL_VirtualLobbyShutdown(0, 0);
					}

					scheduler::once([target, mapname, gametype, max_clients, attempt_id]()
					{
						if (!is_connection_attempt_current(attempt_id))
						{
							return;
						}

						connect_to_server(target, mapname, gametype, max_clients, attempt_id);
					}, scheduler::pipeline::main);
				}, scheduler::pipeline::main);
			});
		}
	};
}

REGISTER_COMPONENT(party::component)
