#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "party.hpp"
#include "dedicated_party.hpp"
#include "dedicated_party_client.hpp"
#include "command.hpp"
#include "scheduler.hpp"
#include "network.hpp"

#include "game/dvars.hpp"
#include "game/ui_scripting/execution.hpp"

#include "ui_scripting.hpp"

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
		const game::dvar_t* change_team_enabled{};

		bool can_change_assigned_team()
		{
			return game::environment::is_dedicated()
				&& change_team_enabled && change_team_enabled->current.enabled;
		}

		bool has_assigned_team_stub(const char client_num)
		{
			const auto has_assigned_team = game::mp::SV_HasAssignedTeam_Internal(client_num);
			return has_assigned_team && !can_change_assigned_team();
		}

		std::uint64_t update_session_team_stub(const char client_num)
		{
			// Preserve the stock propagation from gclient::sessionTeam into the
			// server character info before publishing the new PartyMember team.
			const auto team = utils::hook::invoke<std::uint64_t>(0x547A60_g, client_num);

			if (can_change_assigned_team()
				&& game::mp::SV_HasAssignedTeam_Internal(client_num))
			{
				game::mp::SV_SetAssignedTeam(client_num, static_cast<int>(team));
			}

			return team;
		}

		struct connect_state_t
		{
			game::netadr_s host{};
			std::string challenge{};
			bool host_defined{ false };
			bool query_pending{};
			std::uint64_t attempt_id{};
		};

		connect_state_t connect_state{};

		bool client_map_session_active{};

		struct queued_map_start_t
		{
			std::string map_name{};
			std::string gametype{};
			int map_index{};
			bool set_gametype{};
		};

		std::optional<queued_map_start_t> queued_map_start{};

		enum class listen_map_transition_phase
		{
			none,
			waiting_for_disconnect,
			waiting_for_frontend,
			starting,
		};

		listen_map_transition_phase listen_map_phase{ listen_map_transition_phase::none };
		constexpr auto listen_map_transition_timeout = 90s;

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

		bool is_active_party_host(game::PartyData* party_data)
		{
			return party_data && game::Party_IsRunning(party_data)
				&& game::Party_AreWeHost(party_data);
		}

		bool is_party_host_ready(game::PartyData* party_data)
		{
			return is_active_party_host(party_data)
				&& !game::Party_IsWaitingForMembers(party_data);
		}

		bool is_unranked_private_match(game::PartyData* party_data)
		{
			return party_data
				&& game::PartySettings_GetPrivateMatch(&party_data->settings) == 1
				&& game::PartySettings_GetRankedMatch(&party_data->settings) == 0;
		}

		bool is_s2x_map_match(game::PartyData* party_data)
		{
			return client_map_session_active && party_data
				&& game::Party_GetPublicMatch(party_data) == 0
				&& game::PartySettings_GetRankedMatch(&party_data->settings) == 0;
		}

		game::PartyData* get_local_game_lobby()
		{
			auto* local_data = game::Lobby_GetLocalClientData(0);
			return game::Lobby_GetPartyDataFromLocalClient(local_data);
		}

		void enable_online_stats_for_map_match(game::PartyData* game_lobby)
		{
			if (!game::environment::is_multiplayer())
			{
				return;
			}

			// MP's stock connect and storage paths only enable online stats when
			// onlinegame is set and privateMatch is clear. Keep publicMatch clear so
			// this S2x-hosted session remains non-advertised and invite-only.
			game::PartySettings_SetPrivateMatch(&game_lobby->settings, false);
			game::PartySettings_SetPublicMatch(&game_lobby->settings, false);
			game::PartySettings_SetRankedMatch(&game_lobby->settings, false);
		}

		int get_private_match_player_limit()
		{
			const auto& mode = game::environment::get_online_mode_info();
			const auto* party_maxplayers = game::Dvar_FindMalleableVar("5321");
			const auto configured_max = party_maxplayers
				? party_maxplayers->current.integer
				: mode.max_players;
			auto max_players = std::clamp(configured_max, 1, mode.max_players);

			const auto hosting_limit = game::Lobby_HowManyPlayersCanWeHost();
			if (hosting_limit > 0)
			{
				max_players = std::min(max_players, hosting_limit);
			}

			return max_players;
		}

		bool validate_client_map_state(const bool server_running)
		{
			if (game::is_local_play())
			{
				console::error(
					"Cannot use map in Local Play. Enter the online Multiplayer or Zombies frontend first.\n");
				return false;
			}

			const auto* onlinegame = game::Dvar_FindMalleableVar("onlinegame");
			if (!onlinegame || !onlinegame->current.enabled)
			{
				console::error(
					"Cannot use map while offline. Enter the online Multiplayer or Zombies frontend first.\n");
				return false;
			}

			if (!server_running && !game::virtual_lobby_loaded())
			{
				console::error(
					"Cannot use map from this screen. Enter the online Multiplayer or Zombies frontend first.\n");
				return false;
			}

			auto* private_party = game::Party_GetPrivatePartyData();
			if (!server_running && !is_party_host_ready(private_party))
			{
				console::error(
					"Cannot use map until the online private party is ready and you are its host.\n");
				return false;
			}

			auto* game_lobby = game::Lobby_GetPartyData(0);
			if (game_lobby && game::Party_IsRunning(game_lobby)
				&& (!game::Party_AreWeHost(game_lobby)
					|| (!is_unranked_private_match(game_lobby)
						&& !is_s2x_map_match(game_lobby))))
			{
				console::error(
					"Cannot use map from a public, ranked, or joined lobby. Leave it and start an online private match.\n");
				return false;
			}

			return true;
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

		std::string get_map_session_gametype()
		{
			if (!client_map_session_active || !game::environment::is_multiplayer())
			{
				return {};
			}

			auto* game_lobby = game::Lobby_GetPartyData(0);
			const auto* gametype = game_lobby
				? game::Party_GetGameType(game_lobby)
				: nullptr;
			return gametype ? gametype : "";
		}

		void install_map_lobby_functions()
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

			lobby["GetS2xMapGameType"] = []
			{
				return get_map_session_gametype();
			};
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

		std::string get_gametype_or_default(const command::params& params,
			const std::string& map_name, bool& set_gametype)
		{
			set_gametype = params.size() >= 3;
			if (set_gametype)
			{
				return params[2];
			}

			const auto& mode = game::environment::get_online_mode_info();
			if (game::environment::is_zombies())
			{
				set_gametype = true;
				return std::string{ mode.default_gametype };
			}

			if (map_name.starts_with("mp_raid_"))
			{
				set_gametype = true;
				return "raid";
			}

			if (map_name.ends_with("_dogfight"))
			{
				set_gametype = true;
				return "dogfight";
			}

			const auto* g_gametype = game::Dvar_FindMalleableVar("g_gametype");
			auto gametype = g_gametype && g_gametype->current.string
				? std::string{ g_gametype->current.string }
				: std::string{ mode.default_gametype };

			const auto lower_gametype = utils::string::to_lower(gametype);
			if (lower_gametype == "hub" || lower_gametype == "zombies"
				|| lower_gametype == "raid" || lower_gametype.starts_with("dogfight"))
			{
				set_gametype = true;
				gametype = "war";
			}

			return gametype;
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
			game::Cbuf_AddText(0, "exec default_xboxlive.cfg\n");

			if (game::environment::is_multiplayer()
				&& !game::environment::is_dedicated())
			{
				set_max_agents(6);
			}
		}

		void restart_map()
		{
			*game::sv_map_restart = 1;
			*game::sv_loadScripts = 1;
			*game::sv_migrate = 0;

			game::mp::SV_MapRestart(*game::sv_migrate, *game::sv_loadScripts);
		}

		void start_server_ui()
		{
			if (!queued_map_start)
			{
				return;
			}

			auto map_start = std::move(*queued_map_start);
			queued_map_start.reset();

			// default_xboxlive.cfg and private-party startup can restore the stock
			// frontend gametype (war). Reapply the requested PartySettings immediately
			// before UI_StartServer reads them and queues UI_Map.
			set_party_map_settings(
				map_start.map_name, map_start.gametype);
			set_map_dvars(
				map_start.map_name, map_start.gametype,
				map_start.map_index, map_start.set_gametype);

			*game::sv_migrate = 0;
			dedicated_party_client::reset();
			game::UI_StartServer(0);
		}

		void queue_start_server_ui(const std::string& map_name,
			const std::string& gametype, const int map_index, const bool set_gametype)
		{
			queued_map_start = queued_map_start_t{
				map_name,
				gametype,
				map_index,
				set_gametype,
			};

			game::Cbuf_AddCall(reinterpret_cast<void*>(start_server_ui));
		}

		void configure_online_private_map(const std::string& map_name,
			const std::string& gametype, const int map_index, const bool set_gametype,
			game::PartyData* game_lobby)
		{
			set_party_map_settings(map_name, gametype);
			set_map_dvars(map_name, gametype, map_index, set_gametype);

			// PartyHost_StartParty reads this value when it creates the session, so it
			// must be set before xstartprivatematch rather than repaired after the fact.
			game::Party_SetMaxClients(
				game_lobby, get_private_match_player_limit());
		}

		void publish_online_private_map()
		{
			// Stock UpdatePrivateMatchMaxPlayers republishes the private/public slot
			// split after the game party has been created.
			game::Cbuf_AddText(
				0, "xtogprivateslots; xtogprivateslots; xsessionupdate;\n");

			// This is Engine.SendJoinedLobbyMsgToParty from the stock Custom Match flow.
			game::PartyHost_NotifyPrivateMatchCreated();
		}

		void start_online_private_map(const std::string& map_name,
			const std::string& gametype, const int map_index, const bool set_gametype)
		{
			client_map_session_active = false;

			auto* game_lobby = get_local_game_lobby();
			if (!game_lobby)
			{
				console::error("Unable to access the online game party.\n");
				return;
			}

			// Stock Custom Match sets these fields and its map defaults before
			// xstartprivatematch. CL_Live_StartPrivateMatchHost then stops and wakes the
			// game PartyData, clears its registered session users, rebuilds the session,
			// and calls PartyHost_StartParty. SV_DirectConnect treats a registered-user
			// slot match as a reconnect and otherwise may replace that slot's client.
			// Calling that handler directly keeps map immediate while performing the
			// same party/session initialization the old direct path omitted.
			game::PartySettings_SetPrivateMatch(&game_lobby->settings, true);
			game::PartySettings_SetPublicMatch(&game_lobby->settings, false);
			game::PartySettings_SetRankedMatch(&game_lobby->settings, false);

			perform_game_init();

			configure_online_private_map(
				map_name, gametype, map_index, set_gametype, game_lobby);

			game::CL_Live_StartPrivateMatchHost();

			// Stock host startup activates the game party and enables its match-rules
			// data. MatchRules.SetData rejects the update before this point.
			if (game::environment::is_multiplayer()
				&& !set_match_rules_gametype(gametype))
			{
				console::warn("Unable to update private-match rules for gametype '%s'.\n",
					gametype.data());
			}

			// Create the session through the private-match path first, then expose the
			// stock online-stats state before publishing the PartySettings to clients.
			enable_online_stats_for_map_match(game_lobby);
			publish_online_private_map();

			// Engine.StartServer in the stock LUI flow queues UI_StartServer rather
			// than invoking it in the xstartprivatematch command's frame. UI_StartServer
			// subsequently queues UI_Map, preserving the two-stage stock transition.
			queue_start_server_ui(map_name, gametype, map_index, set_gametype);
			client_map_session_active = true;
		}

		void change_listen_map(const std::string& map_name, const std::string& gametype,
			const int map_index, const bool set_gametype)
		{
			auto* game_lobby = game::Lobby_GetPartyData(0);
			if (!game_lobby || !is_active_party_host(game_lobby))
			{
				console::error("Unable to access the hosted game party for the map change.\n");
				return;
			}

			listen_map_phase = listen_map_transition_phase::waiting_for_disconnect;
			const auto start_time = std::chrono::steady_clock::now();

			// Stock S1 only host-preloads a match when no server is running, and S1x's
			// map command leaves a running match before invoking StartServer. S2's direct
			// UI_Map path passes mapIsPreloaded=false; using it while the server is live
			// unloads the current UI zones before LUI reloads main.lua. Return to the
			// online frontend first, then create the replacement private match through
			// the same initialized path as the initial map command.
			const auto* args = "Leave";
			game::UI_RunMenuScript(0, &args);

			scheduler::schedule([map_name, gametype, map_index, set_gametype, start_time]
			{
				if (listen_map_phase == listen_map_transition_phase::none)
				{
					return scheduler::cond_end;
				}

				if (std::chrono::steady_clock::now() - start_time
					>= listen_map_transition_timeout)
				{
					console::error("Listen map transition to '%s' timed out.\n",
						map_name.data());
					if (!game::is_server_running())
					{
						client_map_session_active = false;
					}
					queued_map_start.reset();
					listen_map_phase = listen_map_transition_phase::none;
					return scheduler::cond_end;
				}

				if (listen_map_phase == listen_map_transition_phase::waiting_for_frontend
					&& !com_sv_running() && !game::SV_Loaded() && *game::frontend_state != 0)
				{
					listen_map_phase = listen_map_transition_phase::starting;
					start_online_private_map(map_name, gametype, map_index, set_gametype);

					if (!client_map_session_active)
					{
						listen_map_phase = listen_map_transition_phase::none;
						return scheduler::cond_end;
					}
				}

				if (listen_map_phase == listen_map_transition_phase::starting
					&& game::is_server_running()
					&& get_current_mapname() == map_name
					&& get_current_gametype() == gametype)
				{
					listen_map_phase = listen_map_transition_phase::none;
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
			if (listen_map_phase == listen_map_transition_phase::waiting_for_disconnect)
			{
				listen_map_phase = listen_map_transition_phase::waiting_for_frontend;
			}
			else if (listen_map_phase != listen_map_transition_phase::none)
			{
				// A second disconnect supersedes the command-owned transition.
				listen_map_phase = listen_map_transition_phase::none;
			}

			party::cancel_pending_connection();
			client_map_session_active = false;
			queued_map_start.reset();
			disconnect_command_hook.invoke<void>();
			dedicated_party_client::reset();
		}

		void start_map(const command::params& params)
		{
			if (params.size() < 2)
			{
				console::info("usage: map <mapname> [gametype]: loads a map with an optional gametype\n");
				return;
			}

			if (queued_map_start
				|| listen_map_phase != listen_map_transition_phase::none)
			{
				console::error("A map transition is already in progress.\n");
				return;
			}

			const auto map_name = utils::string::to_lower(params[1]);
			bool set_gametype{};
			auto gametype = get_gametype_or_default(params, map_name, set_gametype);
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
			const auto is_dedicated = game::environment::is_dedicated();
			if (!is_dedicated && !validate_client_map_state(server_running))
			{
				return;
			}

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

			if (server_running && get_current_mapname() == map_name
				&& (!set_gametype || get_current_gametype() == gametype))
			{
				restart_map();
				return;
			}

			if (!is_dedicated && server_running)
			{
				change_listen_map(map_name, gametype, map_index, set_gametype);
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

			start_online_private_map(map_name, gametype, map_index, set_gametype);
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

	bool set_match_rules_gametype(const std::string& gametype)
	{
		if (!*game::hks::lui_lua_state)
		{
			return false;
		}

		game::LUI_EnterCriticalSection();

		bool success = false;
		try
		{
			const auto match_rules_value = ui_scripting::get_globals().get("MatchRules");
			if (match_rules_value.is<ui_scripting::table>())
			{
				const auto set_data = match_rules_value.as<ui_scripting::table>().get("SetData");
				if (set_data.is<ui_scripting::function>())
				{
					const auto result = set_data("gametype", gametype);
					success = !result.empty() && result[0].is<bool>() && result[0].as<bool>();
				}
			}
		}
		catch (const std::exception& e)
		{
			console::error("Failed to update match rules: %s\n", e.what());
		}

		game::LUI_LeaveCriticalSection();
		return success;
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
				change_team_enabled = game::Dvar_RegisterBool(
					"3193", true, game::DVAR_FLAG_READ);

				if (game::environment::is_dedicated())
				{
					// Public lobbies normally lock gclient::sessionTeam to the team
					// assigned by PartyHost_PreMatch. Permit the stock team_select GSC
					// path to change it, then mirror that authoritative value back to
					// PartyData so reconnects and lobby presentation stay consistent.
					utils::hook::call(0x546128_g, has_assigned_team_stub);
					utils::hook::call(0x546194_g, update_session_team_stub);
				}
			}

			cl_connect_hook.create(game::CL_Connect, cl_connect_stub);
			disconnect_command_hook.create(game::CL_Disconnect_f, disconnect_command_stub);
			if (!game::environment::is_dedicated())
			{
				ui_scripting::on_start(install_map_lobby_functions);
			}

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
