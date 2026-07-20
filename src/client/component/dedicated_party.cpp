#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "dedicated_party.hpp"
#include "party.hpp"
#include "command.hpp"
#include "scheduler.hpp"

#include "console/console.hpp"

#include "game/dvars.hpp"
#include "game/game.hpp"
#include "game/ui_scripting/execution.hpp"

#include "ui_scripting.hpp"

#include <utils/flags.hpp>
#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace dedicated_party
{
	namespace
	{
		constexpr std::ptrdiff_t session_id_offset = 52;
		constexpr std::ptrdiff_t session_host_address_offset = 60;
		constexpr std::ptrdiff_t session_key_offset = 97;
		constexpr std::ptrdiff_t party_settings_offset = 0x250;
		constexpr std::ptrdiff_t party_active_client_offset = 0x186400;
		constexpr std::ptrdiff_t party_launch_deadline_offset = 0x186430;
		constexpr std::ptrdiff_t party_host_state_offset = 0x186490;
		constexpr std::uint32_t party_host_state_mask = 0xFC;

		enum class dedicated_party_stage
		{
			inactive,
			waiting_for_private_party,
			waiting_for_game_lobby,
			waiting_for_match_settings,
			waiting_for_intermission,
			waiting_for_countdown,
			match_running,
			ending_match,
			waiting_for_lobby,
		};

		struct dedicated_party_state_t
		{
			dedicated_party_stage stage{ dedicated_party_stage::inactive };
			std::vector<dedicated_match_t> rotation{};
			std::optional<dedicated_match_t> requested_next_match{};
			std::optional<dedicated_match_t> requested_rotation_match{};
			dedicated_match_t current_match{};
			std::size_t next_rotation_index{};
			void* private_party{};
			void* game_lobby{};
			std::chrono::steady_clock::time_point stage_started{};
		};

		dedicated_party_state_t dedicated_party_state{};
		game::dvar_t* dedicated_lobby_time{};
		game::dvar_t* sv_maprotation{};
		bool map_rotate_requested{};

		void set_stage(const dedicated_party_stage stage)
		{
			dedicated_party_state.stage = stage;
			dedicated_party_state.stage_started = std::chrono::steady_clock::now();
		}

		void* get_private_party_data()
		{
			return game::Party_GetPrivatePartyData();
		}

		bool is_active_party_host(void* party_data)
		{
			if (!party_data)
			{
				return false;
			}

			// These are the stock Party_IsRunning/host-readiness predicates used before
			// PartyHost_Frame is allowed to advance its prematch state machine.
			return game::Party_IsRunning(party_data) && game::Party_AreWeHost(party_data);
		}

		bool is_party_host_ready(void* party_data)
		{
			return is_active_party_host(party_data) && !game::Party_IsWaitingForMembers(party_data);
		}

		void set_party_is_private_match(void* party_data, const bool private_match)
		{
			if (!party_data)
			{
				return;
			}

			auto* settings = reinterpret_cast<void*>(
				reinterpret_cast<std::uintptr_t>(party_data) + party_settings_offset);
			game::PartySettings_SetPrivateMatch(settings, private_match);
			game::PartySettings_SetPublicMatch(settings, !private_match);
		}

		void set_party_is_ranked_match(void* party_data, const bool ranked)
		{
			if (!party_data)
			{
				return;
			}

			auto* settings = reinterpret_cast<void*>(
				reinterpret_cast<std::uintptr_t>(party_data) + party_settings_offset);
			game::PartySettings_SetRankedMatch(settings, ranked);
		}

		void configure_public_lobby()
		{
			set_party_is_private_match(dedicated_party_state.game_lobby, false);
			set_party_is_ranked_match(dedicated_party_state.game_lobby, false);
		}

		void set_game_is_private_match(const int local_client_num, const bool private_match)
		{
			auto* local_data = game::Lobby_GetLocalClientData(local_client_num);
			auto* party_data = game::Lobby_GetPartyDataFromLocalClient(local_data);
			set_party_is_private_match(party_data, private_match);
		}

		void set_game_is_ranked_match(const int local_client_num, const bool ranked)
		{
			auto* local_data = game::Lobby_GetLocalClientData(local_client_num);
			auto* party_data = game::Lobby_GetPartyDataFromLocalClient(local_data);
			set_party_is_ranked_match(party_data, ranked);
		}

		std::uint32_t xpartygo_private_match_stub(void*)
		{
			return 1;
		}

		void reset_party_launch_deadline()
		{
			if (!dedicated_party_state.game_lobby)
			{
				return;
			}

			// PartyHost_Frame only rebuilds its native countdown deadline when this
			// field is zero. The private postgame state reset leaves the old value intact.
			*reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(
				dedicated_party_state.game_lobby) + party_launch_deadline_offset) = 0;
		}

		bool party_is_owned_by_local_client_zero(void* party_data)
		{
			if (!party_data)
			{
				return false;
			}

			// Party_Frame reads the stored PartyActiveClient from this field for hosts.
			const auto* active_client = reinterpret_cast<const std::uint32_t*>(
				reinterpret_cast<std::uintptr_t>(party_data) + party_active_client_offset);
			return active_client[0] == 0;
		}

		std::uint32_t get_party_host_state(void* party_data)
		{
			return *reinterpret_cast<const std::uint32_t*>(
				reinterpret_cast<std::uintptr_t>(party_data) + party_host_state_offset)
				& party_host_state_mask;
		}

		void reset_party_launch_state()
		{
			reset_party_launch_deadline();

			if (dedicated_party_state.game_lobby
				&& get_party_host_state(dedicated_party_state.game_lobby) != 4)
			{
				// PartyHost_Frame uses this native setter to return an interrupted launch
				// to prematch state 4 and broadcast the new state to party members.
				game::PartyHost_SetState(dedicated_party_state.game_lobby, 4);
			}
		}

		bool hosted_game_lobby_is_ready()
		{
			const auto game_lobby = dedicated_party_state.game_lobby;

			return game::virtual_lobby_loaded()
				&& game_lobby
				&& is_active_party_host(game_lobby)
				&& party_is_owned_by_local_client_zero(game_lobby)
				&& get_party_host_state(game_lobby) == 4;
		}

		bool is_session_hex_string(const std::string& value, const std::size_t expected_size)
		{
			return value.size() == expected_size
				&& std::all_of(value.begin(), value.end(), [](const unsigned char character)
				{
					return std::isxdigit(character) != 0;
				});
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

		bool make_match(const std::string& map_name, const std::string& gametype,
			dedicated_match_t& match)
		{
			int map_index = 0;
			if (!party::resolve_map_index(map_name, map_index))
			{
				return false;
			}

			if (!party::validate_gametype(gametype))
			{
				console::error("Gametype '%s' is not available locally.\n", gametype.data());
				return false;
			}

			match = { map_name, gametype, map_index };
			return true;
		}

		bool parse_map_rotation(std::vector<dedicated_match_t>& rotation)
		{
			rotation.clear();
			if (!sv_maprotation || !sv_maprotation->current.string
				|| !sv_maprotation->current.string[0])
			{
				console::error("Dedicated map rotation: sv_maprotation is empty.\n");
				return false;
			}

			std::istringstream stream{ sv_maprotation->current.string };
			std::string token{};
			auto gametype = utils::string::to_lower(party::loaded_gametype());
			bool gametype_needs_map = false;

			while (stream >> token)
			{
				const auto keyword = utils::string::to_lower(token);
				if (keyword == "gametype")
				{
					if (gametype_needs_map)
					{
						console::error(
							"Dedicated map rotation: gametype '%s' is not followed by a map.\n",
							gametype.data());
					}

					if (!(stream >> gametype))
					{
						console::error(
							"Dedicated map rotation: 'gametype' is missing its value.\n");
						break;
					}

					gametype = utils::string::to_lower(gametype);
					gametype_needs_map = true;
					continue;
				}

				if (keyword == "map")
				{
					std::string map_name{};
					if (!(stream >> map_name))
					{
						console::error("Dedicated map rotation: 'map' is missing its value.\n");
						break;
					}

					map_name = utils::string::to_lower(map_name);
					gametype_needs_map = false;

					dedicated_match_t match{};
					if (!make_match(map_name, gametype, match))
					{
						console::error(
							"Dedicated map rotation: skipping invalid entry '%s %s'.\n",
							map_name.data(), gametype.data());
						continue;
					}

					rotation.emplace_back(std::move(match));
					continue;
				}

				console::error(
					"Dedicated map rotation: unexpected token '%s'; expected 'gametype' or 'map'.\n",
					token.data());
			}

			if (gametype_needs_map)
			{
				console::error(
					"Dedicated map rotation: gametype '%s' is not followed by a map.\n",
					gametype.data());
			}

			if (rotation.empty())
			{
				console::error("Dedicated map rotation: no valid matches were found.\n");
				return false;
			}

			return true;
		}

		void handle_map_rotate()
		{
			if (!sv_maprotation)
			{
				map_rotate_requested = true;
				return;
			}

			if (!is_active())
			{
				std::vector<dedicated_match_t> rotation{};
				if (!parse_map_rotation(rotation) || !set_rotation(std::move(rotation)))
				{
					return;
				}

				map_rotate_requested = false;
				start();
				return;
			}

			if (!rotate())
			{
				console::error("Dedicated map rotation is not available.\n");
			}
		}

		void apply_lobby_time()
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
				console::error("Dedicated party: failed to update match rules: %s\n", e.what());
			}

			game::LUI_LeaveCriticalSection();
			return success;
		}

		void prepare_match_settings(const dedicated_match_t& match)
		{
			if (!set_match_rules_gametype(match.gametype))
			{
				console::error("Dedicated party: failed to select match-rules gametype '%s'.\n",
					match.gametype.data());
			}

			// Stock lobby setup restores its gameplay defaults asynchronously. The
			// selected map and gametype are applied on the following main frame.
			game::Cbuf_AddText(0, "exec default_xboxlive.cfg\n");
		}

		void prepare_postmatch_lobby()
		{
			apply_lobby_time();
			configure_public_lobby();
			reset_party_launch_state();
		}

		void apply_dedicated_match_settings(const dedicated_match_t& match)
		{
			party::apply_map_settings(match.map_name, match.gametype, match.map_index);

			// PartyHost_StartMatch sends the hosted party fields in its go message, but
			// initializes g_gametype from the launch state selected by sub_470D30(1).
			// UI_SetMap may target a different state while the public lobby is active.
			auto* launch_state = game::Lobby_GetPartyDataFromLocalClient(
				game::Lobby_GetLocalClientData(1));
			if (launch_state)
			{
				game::Party_SetMapName(launch_state, match.map_name.data());
				game::Party_SetGameType(launch_state, match.gametype.data());
			}
		}

		void apply_match(const dedicated_match_t& match)
		{
			apply_lobby_time();
			reset_party_launch_state();

			configure_public_lobby();
			prepare_match_settings(match);

			dedicated_party_state.current_match = match;

			console::info("Dedicated party: selected next map %s %s.\n",
				match.map_name.data(), match.gametype.data());
			set_stage(dedicated_party_stage::waiting_for_match_settings);
		}

		std::int64_t party_host_auto_start_stub(void* party_data, void* active_client)
		{
			if (party_data == dedicated_party_state.game_lobby)
			{
				if (dedicated_party_state.stage != dedicated_party_stage::waiting_for_countdown)
				{
					return 0;
				}

			}

			return game::PartyHost_AutoStart(party_data, active_client);
		}

		std::int64_t party_host_start_match_stub(void* party_data, void* active_client)
		{
			if (party_data == dedicated_party_state.game_lobby
				&& dedicated_party_state.stage == dedicated_party_stage::waiting_for_countdown)
			{
				auto* launch_state = game::Lobby_GetPartyDataFromLocalClient(
					game::Lobby_GetLocalClientData(1));
				const auto* party_gametype = game::Party_GetGameType(party_data);
				const auto* launch_gametype = launch_state
					? game::Party_GetGameType(launch_state)
					: "";
				const std::string_view party_gametype_value = party_gametype ? party_gametype : "";
				const std::string_view launch_gametype_value = launch_gametype ? launch_gametype : "";
				if (dedicated_party_state.current_match.gametype != party_gametype_value
					|| dedicated_party_state.current_match.gametype != launch_gametype_value)
				{
					console::info(
						"Dedicated party: correcting launch gametype party='%s' state='%s' to '%s'.\n",
						party_gametype ? party_gametype : "",
						launch_gametype ? launch_gametype : "",
						dedicated_party_state.current_match.gametype.data());
				}

				// PartyHost_StartMatch reads these fields for the final go message and
				// server preload. Public auto-start may have selected a playlist entry.
				apply_dedicated_match_settings(dedicated_party_state.current_match);
			}

			return game::PartyHost_StartMatch(party_data, active_client);
		}

		void fail_lifecycle(const char* message)
		{
			console::error("Dedicated party: %s\n", message);
			dedicated_party_state = {};
		}

		bool stage_timed_out(const std::chrono::seconds timeout)
		{
			return std::chrono::steady_clock::now() - dedicated_party_state.stage_started >= timeout;
		}

		dedicated_match_t take_rotation_match()
		{
			const auto index = dedicated_party_state.next_rotation_index;
			dedicated_party_state.next_rotation_index =
				(index + 1) % dedicated_party_state.rotation.size();
			return dedicated_party_state.rotation[index];
		}

		dedicated_match_t take_next_match()
		{
			if (dedicated_party_state.requested_next_match.has_value())
			{
				auto match = std::move(*dedicated_party_state.requested_next_match);
				dedicated_party_state.requested_next_match.reset();
				return match;
			}

			if (dedicated_party_state.requested_rotation_match.has_value())
			{
				auto match = std::move(*dedicated_party_state.requested_rotation_match);
				dedicated_party_state.requested_rotation_match.reset();
				return match;
			}

			return take_rotation_match();
		}

		bool run_lifecycle()
		{
			switch (dedicated_party_state.stage)
			{
			case dedicated_party_stage::waiting_for_private_party:
			{
				auto* private_party = get_private_party_data();
				if (game::CL_IsLocalClientInGame(0)
					&& is_party_host_ready(private_party)
					&& party_is_owned_by_local_client_zero(private_party))
				{
					dedicated_party_state.private_party = private_party;
					console::info("Dedicated party: private party created.\n");

					apply_lobby_time();
					// The stock command creates the hosted game-lobby object as a private match.
					// Once created, apply_match keeps that game lobby public for its lifetime.
					set_game_is_private_match(0, true);
					set_game_is_ranked_match(0, false);

					set_stage(dedicated_party_stage::waiting_for_game_lobby);

					// Run the host-creation command after this PartyHost frame returns. The
					// active-client gate above ensures the virtual-lobby handshake is complete.
					game::Cbuf_AddText(0, "xstartprivatematch\n");
				}
				else if (stage_timed_out(30s))
				{
					fail_lifecycle("online private-party creation timed out.");
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
					game::PartyHost_NotifyPrivateMatchCreated();
					console::info("Dedicated party: game lobby created.\n");
					apply_match(take_next_match());
				}
				else if (stage_timed_out(30s))
				{
					fail_lifecycle("private-match lobby creation timed out.");
					return scheduler::cond_end;
				}
				break;
			}

			case dedicated_party_stage::waiting_for_match_settings:
			{
				// default_xboxlive.cfg executes in the stock main frame before this
				// pipeline. Apply and publish the requested mode afterward so public
				// team assignment and remote lobby state use the same gametype.
				apply_dedicated_match_settings(dedicated_party_state.current_match);
				game::Cbuf_AddText(0, "xupdatepartystate\n");
				console::info("Dedicated party: countdown started.\n");
				set_stage(dedicated_party_stage::waiting_for_intermission);
				break;
			}

			case dedicated_party_stage::waiting_for_intermission:
			{
				if (stage_timed_out(std::chrono::seconds{
					dedicated_lobby_time->current.integer }))
				{
					// Stock public matches prepare their teams before the final start request.
					// The dedicated xpartygo shim otherwise takes the private-match shortcut
					// and leaves Raid party members without an assigned team.
					if (get_party_host_state(dedicated_party_state.game_lobby) == 4)
					{
						game::PartyHost_PreMatch(dedicated_party_state.game_lobby, 0);
					}

					if (get_party_host_state(dedicated_party_state.game_lobby) != 32)
					{
						break;
					}

					set_stage(dedicated_party_stage::waiting_for_countdown);

					// This is the stock outer action used by the lobby Start button. The
					// dedicated command shims only relax its private-match eligibility checks.
					game::Cbuf_AddText(0, "xpartygo\n");
				}
				break;
			}

			case dedicated_party_stage::waiting_for_countdown:
			{
				if (party::server_running() && game::SV_Loaded()
					&& !game::virtual_lobby_loaded()
					&& party::loaded_map_name() == dedicated_party_state.current_match.map_name
					&& party::loaded_gametype() == dedicated_party_state.current_match.gametype)
				{
					if (has_local_gameplay_client())
					{
						console::error("Dedicated party: local client 0 occupied a gameplay slot.\n");
					}

					console::info("Dedicated party: match started.\n");
					set_stage(dedicated_party_stage::match_running);
				}
				else if (stage_timed_out(60s))
				{
					fail_lifecycle("native match start timed out.");
					return scheduler::cond_end;
				}
				break;
			}

			case dedicated_party_stage::match_running:
				if (!party::server_running() && !game::SV_Loaded())
				{
					prepare_postmatch_lobby();
					console::info("Dedicated party: match ended.\n");
					set_stage(dedicated_party_stage::waiting_for_lobby);
				}
				break;

			case dedicated_party_stage::ending_match:
				if (!party::server_running() && !game::SV_Loaded())
				{
					prepare_postmatch_lobby();
					console::info("Dedicated party: match ended.\n");
					set_stage(dedicated_party_stage::waiting_for_lobby);
				}
				else if (stage_timed_out(30s))
				{
					fail_lifecycle("normal match end timed out.");
					return scheduler::cond_end;
				}
				break;

			case dedicated_party_stage::waiting_for_lobby:
				if (!party::server_running() && !game::SV_Loaded()
					&& hosted_game_lobby_is_ready())
				{
					console::info("Dedicated party: returned to lobby.\n");
					apply_match(take_next_match());
				}
				else if (stage_timed_out(30s))
				{
					fail_lifecycle("the persistent lobby did not recover after match end.");
					return scheduler::cond_end;
				}
				break;

			case dedicated_party_stage::inactive:
				return scheduler::cond_end;
			}

			return scheduler::cond_continue;
		}

		void end_match()
		{
			if (!game::environment::is_dedi()
				|| dedicated_party_state.stage != dedicated_party_stage::match_running
				|| !party::server_running())
			{
				console::error("Dedicated party: no match is currently running.\n");
				return;
			}

			// This is S2's normal match-end entry. It records party results and sets the
			// stock end flag/reason consumed by SV_Frame; it does not shut the server down here.
			game::PartyHost_EndMatch();
			set_stage(dedicated_party_stage::ending_match);
		}
	}

	void start()
	{
		if (!game::environment::is_dedi() || is_active()
			|| !game::virtual_lobby_loaded())
		{
			return;
		}

		if (dedicated_party_state.rotation.empty() && map_rotate_requested)
		{
			std::vector<dedicated_match_t> rotation{};
			if (!parse_map_rotation(rotation) || !set_rotation(std::move(rotation)))
			{
				return;
			}

			map_rotate_requested = false;
		}

		if (dedicated_party_state.rotation.empty())
		{
			return;
		}

		auto rotation = std::move(dedicated_party_state.rotation);
		dedicated_party_state = {};
		dedicated_party_state.rotation = std::move(rotation);

		apply_lobby_time();
		set_stage(dedicated_party_stage::waiting_for_private_party);
		scheduler::schedule(run_lifecycle, scheduler::pipeline::main);

		auto* private_party = get_private_party_data();
		if (!is_party_host_ready(private_party) || !party_is_owned_by_local_client_zero(private_party))
		{
			game::Cbuf_AddText(0, "xstartprivateparty\n");
			console::info("Waiting for the online private party to initialize...\n");
		}
	}

	bool is_active()
	{
		return dedicated_party_state.stage != dedicated_party_stage::inactive;
	}

	std::string get_current_gametype()
	{
		if (!is_active())
		{
			return {};
		}

		return dedicated_party_state.current_match.gametype;
	}

	bool set_rotation(std::vector<dedicated_match_t> rotation)
	{
		if (is_active() || rotation.empty())
		{
			return false;
		}

		dedicated_party_state.rotation = std::move(rotation);
		dedicated_party_state.next_rotation_index = 0;
		dedicated_party_state.requested_next_match.reset();
		dedicated_party_state.requested_rotation_match.reset();

		console::info("Dedicated map rotation:\n");
		for (std::size_t i = 0; i < dedicated_party_state.rotation.size(); ++i)
		{
			const auto& match = dedicated_party_state.rotation[i];
			console::info("%zu. %s %s\n", i + 1,
				match.map_name.data(), match.gametype.data());
		}

		return true;
	}

	bool rotate()
	{
		if (!is_active() || dedicated_party_state.rotation.empty())
		{
			return false;
		}

		dedicated_party_state.requested_rotation_match = take_rotation_match();
		if (!party::server_running()
			&& (dedicated_party_state.stage == dedicated_party_stage::waiting_for_match_settings
				|| dedicated_party_state.stage == dedicated_party_stage::waiting_for_intermission))
		{
			apply_match(take_next_match());
			return true;
		}

		const auto& match = *dedicated_party_state.requested_rotation_match;
		console::info("Next dedicated rotation match set to %s %s.\n",
			match.map_name.data(), match.gametype.data());
		return true;
	}

	bool set_next_match(const std::string& map_name, const std::string& gametype, const int map_index)
	{
		if (!is_active())
		{
			return false;
		}

		dedicated_party_state.requested_next_match = dedicated_match_t{ map_name, gametype, map_index };
		console::info("Next dedicated match set to %s %s.\n", map_name.data(), gametype.data());
		return true;
	}

	bool get_connect_info(connect_info& info)
	{
		if (!game::environment::is_dedi() || !is_active())
		{
			return false;
		}

		auto* game_lobby = game::Lobby_GetPartyData(0);
		if (!game_lobby)
		{
			return false;
		}

		auto* session = static_cast<std::uint8_t*>(game::Session_GetData(0));
		if (!session)
		{
			return false;
		}

		std::array<char, 81> host_address{};
		std::array<char, 33> key{};
		std::array<char, 17> session_id{};

		// SVC_Info reads the hosted session through Session_GetData(0), with the
		// ID at +52, host address at +60, and key at +97. Serialize those fields in
		// the stock CL_Connect command format used by the LAN server browser.
		game::Session_HostAddressToString(session + session_host_address_offset, host_address.data());
		game::Session_KeyToString(session + session_key_offset, key.data());
		game::Session_IdToString(
			*reinterpret_cast<const std::uint64_t*>(session + session_id_offset), session_id.data());

		if (dedicated_party_state.current_match.map_name.empty()
			|| dedicated_party_state.current_match.gametype.empty())
		{
			return false;
		}

		info.host_address = host_address.data();
		info.key = key.data();
		info.session_id = session_id.data();
		info.map_name = dedicated_party_state.current_match.map_name;
		info.gametype = dedicated_party_state.current_match.gametype;

		return is_session_hex_string(info.host_address, 80)
			&& is_session_hex_string(info.key, 32)
			&& is_session_hex_string(info.session_id, 16)
			&& !info.map_name.empty()
			&& !info.gametype.empty();
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::is_dedi())
			{
				return;
			}

			dedicated_lobby_time = game::Dvar_RegisterInt(
				"dedicated_lobby_time", 60, 0, 120, game::DVAR_FLAG_NONE);
			map_rotate_requested = utils::flags::has_flag("+map_rotate");

			scheduler::once([]
			{
				sv_maprotation = game::Dvar_RegisterString(
					"sv_maprotation", "", game::DVAR_FLAG_NONE);
			}, scheduler::pipeline::main);

			// CL_Live_PartyGo checks the private-match flag three times: before leaving
			// the virtual lobby, as the fallback when the normal local host check fails,
			// and before requesting the match. Only this command sees the dedicated
			// public game lobby as private; the stored party state remains public.
			utils::hook::call(0x7F18C_g, xpartygo_private_match_stub);
			utils::hook::call(0x7F1B7_g, xpartygo_private_match_stub);
			utils::hook::call(0x7F1C3_g, xpartygo_private_match_stub);

			// PartyHost_Frame has separate public/private auto-start sites. Both stay
			// gated until our dedicated intermission has elapsed.
			utils::hook::call(0x48B605_g, party_host_auto_start_stub);
			utils::hook::call(0x48B768_g, party_host_auto_start_stub);

			// This is the final PartyHost_StartMatch call after public playlist setup.
			// Reapply the rotation settings before it broadcasts the go message.
			utils::hook::call(0x48B214_g, party_host_start_match_stub);

			command::add("dedicated_end_match", [](const command::params&)
			{
				end_match();
			});

			command::add("map_rotate", handle_map_rotate);
		}
	};
}

REGISTER_COMPONENT(dedicated_party::component)
