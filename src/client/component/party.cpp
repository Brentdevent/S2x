#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "party.hpp"
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
		utils::hook::detour dedicated_match_end_hook;
		utils::hook::detour party_atomic_setup_potential_host_hook;

		// Technically max clients is 48, but needs more patches to work properly
		constexpr int total_max_clients = 18;
		constexpr std::ptrdiff_t session_id_offset = 52;
		constexpr std::ptrdiff_t session_host_address_offset = 60;
		constexpr std::ptrdiff_t session_key_offset = 97;

		struct party_connect_info_t
		{
			std::string host_address{};
			std::string key{};
			std::string session_id{};
			std::string map_name{};
			std::string gametype{};
		};

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
		};

		hosted_party_join_state_t hosted_party_join_state{};

		struct dedicated_match_t
		{
			std::string map_name{};
			std::string gametype{};
			int map_index{};
		};

		enum class dedicated_party_stage
		{
			inactive,
			waiting_for_private_party,
			waiting_for_game_lobby,
			waiting_for_countdown,
			match_running,
			ending_match,
			waiting_for_lobby,
		};

		struct dedicated_party_state_t
		{
			dedicated_party_stage stage{ dedicated_party_stage::inactive };
			std::array<dedicated_match_t, 2> rotation{};
			std::optional<dedicated_match_t> requested_next_match{};
			dedicated_match_t current_match{};
			std::size_t rotation_index{};
			void* private_party{};
			void* game_lobby{};
			bool countdown_logged{};
			std::chrono::steady_clock::time_point stage_started{};
		};

		dedicated_party_state_t dedicated_party_state{};
		game::dvar_t* dedicated_lobby_time{};

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

		void set_dedicated_party_stage(const dedicated_party_stage stage)
		{
			dedicated_party_state.stage = stage;
			dedicated_party_state.stage_started = std::chrono::steady_clock::now();
		}

		void* get_private_party_data()
		{
			return utils::hook::invoke<void*>(0x47E350_g);
		}

		bool is_active_party_host(void* party_data)
		{
			if (!party_data)
			{
				return false;
			}

			// These are the stock Party_IsRunning/host-readiness predicates used before
			// PartyHost_Frame is allowed to advance its prematch state machine.
			return utils::hook::invoke<bool>(0x47A6C0_g, party_data)
				&& utils::hook::invoke<bool>(0x47A6F0_g, party_data);
		}

		bool is_party_host_ready(void* party_data)
		{
			return is_active_party_host(party_data)
				&& !utils::hook::invoke<bool>(0x4819A0_g, party_data);
		}

		void set_party_is_private_match(void* party_data, const bool private_match)
		{
			if (!party_data)
			{
				return;
			}

			const auto settings = reinterpret_cast<std::uintptr_t>(party_data) + 0x250;
			utils::hook::invoke<void>(0x1973F0_g, settings, private_match);
			utils::hook::invoke<void>(0x197440_g, settings, !private_match);
		}

		void set_party_is_ranked_match(void* party_data, const bool ranked)
		{
			if (!party_data)
			{
				return;
			}

			const auto settings = reinterpret_cast<std::uintptr_t>(party_data) + 0x250;
			utils::hook::invoke<void>(0x197430_g, settings, ranked);
		}

		void set_game_is_private_match(const int local_client_num, const bool private_match)
		{
			const auto* local_data = utils::hook::invoke<void*>(0x470D30_g, local_client_num);
			const auto party_data = utils::hook::invoke<void*>(0x470F20_g, local_data);
			set_party_is_private_match(party_data, private_match);
		}

		void set_game_is_ranked_match(const int local_client_num, const bool ranked)
		{
			const auto* local_data = utils::hook::invoke<void*>(0x470D30_g, local_client_num);
			const auto party_data = utils::hook::invoke<void*>(0x470F20_g, local_data);
			set_party_is_ranked_match(party_data, ranked);
		}

		const char* dedicated_match_end_stub()
		{
			const auto* reason = dedicated_match_end_hook.invoke<const char*>();

			if (dedicated_party_state.stage == dedicated_party_stage::match_running
				|| dedicated_party_state.stage == dedicated_party_stage::ending_match)
			{
				// Keep gameplay public through stock result processing, then restore the
				// private hosted-lobby mode before the next server frame handles teardown.
				set_party_is_private_match(dedicated_party_state.game_lobby, true);
				set_party_is_ranked_match(dedicated_party_state.game_lobby, false);
			}

			return reason;
		}

		bool party_is_owned_by_local_client_zero(void* party_data)
		{
			if (!party_data)
			{
				return false;
			}

			// Party_Frame reads the stored PartyActiveClient from this field for hosts.
			constexpr std::ptrdiff_t active_client_offset = 1598464;
			const auto* active_client = reinterpret_cast<const std::uint32_t*>(
				reinterpret_cast<std::uintptr_t>(party_data) + active_client_offset);
			return active_client[0] == 0;
		}

		std::uint32_t get_party_host_state(void* party_data)
		{
			constexpr std::ptrdiff_t host_state_offset = 1598608;
			constexpr std::uint32_t host_state_mask = 0xFC;

			return *reinterpret_cast<const std::uint32_t*>(
				reinterpret_cast<std::uintptr_t>(party_data) + host_state_offset) & host_state_mask;
		}

		bool is_party_prematch_state(void* party_data)
		{
			const auto state = get_party_host_state(party_data);
			return state == 4 || state == 8 || state == 16 || state == 32;
		}

		bool dedicated_parties_are_intact()
		{
			const auto private_party = get_private_party_data();
			const auto game_lobby = game::Lobby_GetPartyData(0);

			return private_party == dedicated_party_state.private_party
				&& game_lobby == dedicated_party_state.game_lobby
				&& is_active_party_host(private_party)
				&& is_active_party_host(game_lobby)
				&& party_is_owned_by_local_client_zero(private_party)
				&& party_is_owned_by_local_client_zero(game_lobby);
		}

		bool is_session_hex_string(const std::string& value, const std::size_t expected_size)
		{
			return value.size() == expected_size
				&& std::all_of(value.begin(), value.end(), [](const unsigned char character)
				{
					return std::isxdigit(character) != 0;
				});
		}

		bool get_dedicated_party_connect_info(party_connect_info_t& connect_info)
		{
			if (!game::environment::is_dedi()
				|| dedicated_party_state.stage == dedicated_party_stage::inactive)
			{
				return false;
			}

			auto* game_lobby = game::Lobby_GetPartyData(0);
			const auto session = utils::hook::invoke<std::uintptr_t>(0x6FDE10_g, 0);
			if (!game_lobby || !session)
			{
				return false;
			}

			std::array<char, 81> host_address{};
			std::array<char, 33> key{};
			std::array<char, 17> session_id{};

			// SVC_Info reads the hosted session through sub_6FDE10(0), with the session
			// ID at +52, host address at +60, and key at +97. Serialize those fields in
			// the stock CL_Connect command format used by the LAN server browser.
			utils::hook::invoke<void>(
				0x66EDD0_g, session + session_host_address_offset, host_address.data());
			utils::hook::invoke<void>(
				0x66D970_g, session + session_key_offset, key.data());
			utils::hook::invoke<void>(
				0x66D9B0_g, *reinterpret_cast<const std::uint64_t*>(session + session_id_offset),
				session_id.data());

			const auto* map_name = utils::hook::invoke<const char*>(0x1970E0_g, game_lobby);
			const auto* gametype = utils::hook::invoke<const char*>(0x1970A0_g, game_lobby);
			if (!map_name || !gametype)
			{
				return false;
			}

			connect_info.host_address = host_address.data();
			connect_info.key = key.data();
			connect_info.session_id = session_id.data();
			connect_info.map_name = map_name;
			connect_info.gametype = gametype;

			return is_session_hex_string(connect_info.host_address, 80)
				&& is_session_hex_string(connect_info.key, 32)
				&& is_session_hex_string(connect_info.session_id, 16)
				&& !connect_info.map_name.empty()
				&& !connect_info.gametype.empty();
		}

		bool has_local_gameplay_client()
		{
			auto* clients = *game::mp::svs_clients;
			if (!clients)
			{
				return false;
			}

			for (auto i = 0; i < *game::sv_maxclients; ++i)
			{
				if (clients[i].state != 0 && clients[i].remoteAddress.type == game::NA_LOOPBACK)
				{
					return true;
				}
			}

			return false;
		}

		bool make_dedicated_match(const std::string& map_name, const std::string& gametype,
			dedicated_match_t& match)
		{
			int map_index = 0;
			if (!get_map_index(map_name, map_index))
			{
				return false;
			}

			if (!is_valid_gametype(gametype))
			{
				console::error("Gametype '%s' is not available locally.\n", gametype.data());
				return false;
			}

			match = { map_name, gametype, map_index };
			return true;
		}

		void apply_dedicated_lobby_time()
		{
			if (!dedicated_lobby_time)
			{
				return;
			}

			// PartyHost_Frame builds its launch deadline from the native pregame (4853)
			// and game-start (901) timers. Keep the whole intermission in the countdown.
			game::Dvar_SetIntByName("4853", 0);
			game::Dvar_SetIntByName("901", dedicated_lobby_time->current.integer);
		}

		void apply_dedicated_match(const dedicated_match_t& match)
		{
			apply_dedicated_lobby_time();
			set_party_is_private_match(dedicated_party_state.game_lobby, true);
			set_party_is_ranked_match(dedicated_party_state.game_lobby, false);
			set_party_map_settings(match.map_name, match.gametype);
			set_map_dvars(match.map_name, match.gametype, match.map_index, true);

			dedicated_party_state.current_match = match;
			dedicated_party_state.countdown_logged = false;

			for (std::size_t i = 0; i < dedicated_party_state.rotation.size(); ++i)
			{
				if (dedicated_party_state.rotation[i].map_name == match.map_name
					&& dedicated_party_state.rotation[i].gametype == match.gametype)
				{
					dedicated_party_state.rotation_index = i;
					break;
				}
			}

			console::info("Dedicated party: selected next map %s %s.\n",
				match.map_name.data(), match.gametype.data());
			set_dedicated_party_stage(dedicated_party_stage::waiting_for_countdown);

			// This is the stock outer action used by the private-lobby Start button.
			// PartyHost_Frame owns the resulting countdown, preload, and match start.
			game::Cbuf_AddText(0, "xpartygo\n");
		}

		void fail_dedicated_party_lifecycle(const char* message)
		{
			console::error("Dedicated party: %s\n", message);
			dedicated_party_state = {};
		}

		bool dedicated_party_stage_timed_out(const std::chrono::seconds timeout)
		{
			return std::chrono::steady_clock::now() - dedicated_party_state.stage_started >= timeout;
		}

		dedicated_match_t take_next_dedicated_match()
		{
			if (dedicated_party_state.requested_next_match.has_value())
			{
				auto match = std::move(*dedicated_party_state.requested_next_match);
				dedicated_party_state.requested_next_match.reset();
				return match;
			}

			const auto next_index = (dedicated_party_state.rotation_index + 1)
				% dedicated_party_state.rotation.size();
			return dedicated_party_state.rotation[next_index];
		}

		bool run_dedicated_party_lifecycle()
		{
			switch (dedicated_party_state.stage)
			{
			case dedicated_party_stage::waiting_for_private_party:
			{
				auto* private_party = get_private_party_data();
				if (is_party_host_ready(private_party) && party_is_owned_by_local_client_zero(private_party))
				{
					dedicated_party_state.private_party = private_party;
					console::info("Dedicated party: private party created.\n");

					apply_dedicated_lobby_time();
					// xpartygo only starts a locally hosted countdown for private matches. The
					// gameplay state becomes public again once that start has been committed.
					set_game_is_private_match(0, true);
					set_game_is_ranked_match(0, false);

					set_dedicated_party_stage(dedicated_party_stage::waiting_for_game_lobby);

					// Use the stock outer command so its wrapper establishes the command-scoped
					// host-creation state and runs after this party-frame callback returns.
					game::Cbuf_AddText(0, "xstartprivatematch\n");
				}
				else if (dedicated_party_stage_timed_out(30s))
				{
					fail_dedicated_party_lifecycle("private-party creation timed out.");
					return scheduler::cond_end;
				}
				break;
			}

			case dedicated_party_stage::waiting_for_game_lobby:
			{
				auto* game_lobby = game::Lobby_GetPartyData(0);
				// PartyHost_StartParty installs the host and enters prematch state 4
				// synchronously. Party_IsWaitingForMembers remains true here because the
				// dedicated frontend owner intentionally has no gameplay party member.
				if (is_active_party_host(game_lobby)
					&& party_is_owned_by_local_client_zero(game_lobby)
					&& get_party_host_state(game_lobby) == 4)
				{
					dedicated_party_state.game_lobby = game_lobby;

					// Stock StartPrivateMatch notifies the persistent private party only after
					// the hosted game lobby has completed creation.
					utils::hook::invoke<void>(0x12A2F0_g);
					console::info("Dedicated party: game lobby created.\n");
					apply_dedicated_match(dedicated_party_state.rotation[0]);
				}
				else if (dedicated_party_stage_timed_out(30s))
				{
					fail_dedicated_party_lifecycle("private-match lobby creation timed out.");
					return scheduler::cond_end;
				}
				break;
			}

			case dedicated_party_stage::waiting_for_countdown:
			{
				if (!dedicated_party_state.countdown_logged
					&& get_party_host_state(dedicated_party_state.game_lobby) == 32)
				{
					dedicated_party_state.countdown_logged = true;
					set_party_is_private_match(dedicated_party_state.game_lobby, false);
					set_party_is_ranked_match(dedicated_party_state.game_lobby, false);
					console::info("Dedicated party: countdown started.\n");
				}

				if (com_sv_running() && game::SV_Loaded()
					&& get_current_mapname() == dedicated_party_state.current_match.map_name
					&& get_current_gametype() == dedicated_party_state.current_match.gametype)
				{
					if (has_local_gameplay_client())
					{
						console::error("Dedicated party: local client 0 occupied a gameplay slot.\n");
					}

					console::info("Dedicated party: match started.\n");
					set_dedicated_party_stage(dedicated_party_stage::match_running);
				}
				else if (dedicated_party_stage_timed_out(
					std::chrono::seconds{ dedicated_lobby_time->current.integer + 60 }))
				{
					fail_dedicated_party_lifecycle("native match start timed out.");
					return scheduler::cond_end;
				}
				break;
			}

			case dedicated_party_stage::match_running:
				if (!com_sv_running() && !game::SV_Loaded())
				{
					console::info("Dedicated party: match ended.\n");
					set_dedicated_party_stage(dedicated_party_stage::waiting_for_lobby);
				}
				break;

			case dedicated_party_stage::ending_match:
				if (!com_sv_running() && !game::SV_Loaded())
				{
					console::info("Dedicated party: match ended.\n");
					set_dedicated_party_stage(dedicated_party_stage::waiting_for_lobby);
				}
				else if (dedicated_party_stage_timed_out(30s))
				{
					fail_dedicated_party_lifecycle("normal match end timed out.");
					return scheduler::cond_end;
				}
				break;

			case dedicated_party_stage::waiting_for_lobby:
				if (!com_sv_running() && !game::SV_Loaded()
					&& dedicated_parties_are_intact()
					&& is_party_prematch_state(dedicated_party_state.game_lobby))
				{
					console::info("Dedicated party: returned to lobby.\n");
					apply_dedicated_match(take_next_dedicated_match());
				}
				else if (dedicated_party_stage_timed_out(30s))
				{
					fail_dedicated_party_lifecycle("the persistent lobby did not recover after match end.");
					return scheduler::cond_end;
				}
				break;

			case dedicated_party_stage::inactive:
				return scheduler::cond_end;
			}

			return scheduler::cond_continue;
		}

		void start_dedicated_party_lifecycle()
		{
			if (dedicated_party_state.stage != dedicated_party_stage::inactive)
			{
				return;
			}

			dedicated_party_state = {};
			if (!make_dedicated_match("mp_shipment_s2", "undead", dedicated_party_state.rotation[0])
				|| !make_dedicated_match("mp_airship", "undead", dedicated_party_state.rotation[1]))
			{
				fail_dedicated_party_lifecycle("the proof-of-concept rotation is not available locally.");
				return;
			}

			apply_dedicated_lobby_time();
			set_dedicated_party_stage(dedicated_party_stage::waiting_for_private_party);

			scheduler::schedule(run_dedicated_party_lifecycle, scheduler::pipeline::main);
		}

		void set_next_dedicated_match(const dedicated_match_t& match)
		{
			dedicated_party_state.requested_next_match = match;
			console::info("Next dedicated match set to %s %s.\n",
				match.map_name.data(), match.gametype.data());
		}

		void end_dedicated_match()
		{
			if (!game::environment::is_dedi()
				|| dedicated_party_state.stage != dedicated_party_stage::match_running
				|| !com_sv_running())
			{
				console::error("Dedicated party: no match is currently running.\n");
				return;
			}

			// This is S2's normal match-end entry. It records party results and sets the
			// stock end flag/reason consumed by SV_Frame; it does not shut the server down here.
			utils::hook::invoke<const char*>(0x6DFEB0_g);
			set_dedicated_party_stage(dedicated_party_stage::ending_match);
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
			if (is_dedicated && server_running)
			{
				set_next_dedicated_match({ map_name, gametype, map_index });
				return;
			}

			if (is_dedicated && dedicated_party_state.stage != dedicated_party_stage::inactive)
			{
				set_next_dedicated_match({ map_name, gametype, map_index });
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

				start_dedicated_party_lifecycle();
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
			utils::hook::invoke<void>(
				0x66D9B0_g, *reinterpret_cast<const std::uint64_t*>(session_info), session_id.data());
			return hosted_party_join_state.session_id == session_id.data();
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
				auto* session = utils::hook::invoke<void*>(0x6FDE10_g, session_index);

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
				hosted_party_join_state = {true, target, session_id};
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

			party_connect_info_t party_connect_info{};
			if (get_dedicated_party_connect_info(party_connect_info))
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

	bool dedicated_private_party_ready()
	{
		auto* private_party = get_private_party_data();
		return is_party_host_ready(private_party)
			&& party_is_owned_by_local_client_zero(private_party);
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

			if (game::environment::is_dedi())
			{
				dedicated_lobby_time = game::Dvar_RegisterInt(
					"dedicated_lobby_time", 15, 0, 60, game::DVAR_FLAG_NONE);
				dedicated_match_end_hook.create(0x6DFEB0_g, dedicated_match_end_stub);

				command::add("dedicated_end_match", [](const command::params&)
				{
					end_dedicated_match();
				});
			}

			cl_connect_hook.create(game::CL_Connect, cl_connect_stub);
			if (!game::environment::is_dedi())
			{
				party_atomic_setup_potential_host_hook.create(
					0x497EF0_g, party_atomic_setup_potential_host_stub);
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
