#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "dedicated_party_client.hpp"
#include "command.hpp"
#include "dedicated_party.hpp"
#include "headquarters.hpp"
#include "party.hpp"
#include "network.hpp"
#include "scheduler.hpp"

#include "console/console.hpp"

#include "game/ui_scripting/execution.hpp"

#include "ui_scripting.hpp"

#include <utils/cryptography.hpp>
#include <utils/hook.hpp>
#include <utils/info_string.hpp>
#include <utils/string.hpp>

#include <charconv>

namespace dedicated_party_client
{
	namespace
	{
		constexpr auto hq_preparation_timeout = 120s;

		utils::hook::detour cl_connect_and_preload_map_hook;
		utils::hook::detour party_atomic_setup_potential_host_hook;
		utils::hook::detour party_client_handle_go_hook;
		utils::hook::detour party_client_process_party_state_hook;

		struct hosted_party_join_state_t
		{
			bool active{};
			party::session::kind kind{ party::session::kind::none };
			std::uint64_t attempt_id{};
			std::uint64_t match_sequence{};
			game::netadr_s target{};
			std::string session_id{};
			std::string map_name{};
			std::string gametype{};
			int max_players{};
		};

		struct hosted_dedicated_party_state_t
		{
			game::netadr_s target{};
			std::string session_id{};
			std::uint64_t match_sequence{};
			std::string map_name{};
			std::string gametype{};
			std::string sync_challenge{};
			game::PartyData* game_lobby{};
			int max_players{};
			bool sync_after_next_go{};
		};

		hosted_party_join_state_t hosted_party_join_state{};
		std::uint64_t pending_join_generation{};
		hosted_dedicated_party_state_t hosted_dedicated_party_state{};
		std::string joined_custom_session_id{};
		std::string joined_hq_session_id{};
		bool hosted_dedicated_go_in_progress{};

		bool has_joined_session(game::PartyData* party_data, const int party_type,
			const std::string& joined_session_id)
		{
			if (!party_data || party_data != game::Lobby_GetPartyData(party_type)
				|| joined_session_id.empty()
				|| game::Party_AreWeHost(party_data))
			{
				return false;
			}

			// Lobby_GetSessionData: the PartyData address is reused when the client
			// leaves a server and creates a local lobby. Match the session itself.
			const auto* session = utils::hook::invoke<game::SessionData*>(0x470F50_g, party_data);
			if (!session)
			{
				return false;
			}

			std::array<char, 17> session_id{};
			game::Session_IdToString(session->sessionId, session_id.data());
			return joined_session_id == session_id.data();
		}

		void party_atomic_activate_lobby_stub(game::PartyData* party_data,
			const unsigned int controller_index, const int joining)
		{
			const auto hosted_join = has_dedicated_session(party_data)
				|| has_joined_session(party_data, 0, joined_custom_session_id);
			
			if (hosted_join)
			{
				// PartyAtomic_RequestJoin clears the frontend mode for non-system-link
				// joins. Stock matchmaking starts the lobby again afterward, but our
				// IP-based join activates the received party directly. Restore
				// hub mode before PartyAtomic opens the stock lobby; its LUI predicates,
				// Soldier screen, and virtual-lobby character scene all consume it.
				utils::hook::invoke<void>(0x857A10_g, 1);
			}

			utils::hook::invoke<void>(0x47A720_g, party_data, controller_index, joining);
		}

		utils::hook::detour party_client_kicked_from_lobby_hook;
		std::string lobby_kick_reason{};

		void party_client_kicked_from_lobby_stub(game::PartyData* party_data, void* command_data,
			game::netadr_s* from, game::msg_t* message)
		{
			// S2x hosts send the kick reason as a quoted token after kickedFromLobby.
			const command::params params{};
			lobby_kick_reason = params.size() > 1 ? params[1] : "";
			std::erase_if(lobby_kick_reason, [](const unsigned char c) { return c < ' ' || c == 0x7F; });
			if (lobby_kick_reason.size() > 128)
			{
				lobby_kick_reason.resize(128);
			}

			party_client_kicked_from_lobby_hook.invoke<void>(party_data, command_data, from, message);
			lobby_kick_reason.clear();
		}

		void show_lobby_kick_message_stub(const char* message)
		{
			if (lobby_kick_reason.empty())
			{
				utils::hook::invoke<void>(0x49F120_g, message);
				return;
			}

			// Same text as an in-game kick; 0x1F makes the localizer keep the reason literal.
			const auto text = "EXE_PLAYERKICKED\x1F - " + lobby_kick_reason;
			utils::hook::invoke<void>(0x49F120_g, text.data());
		}

		int get_hosted_dedicated_party_member_count()
		{
			if ((game::environment::is_dedicated() && !dedicated_party::is_active())
				|| (!game::environment::is_dedicated()
					&& hosted_dedicated_party_state.session_id.empty()))
			{
				return -1;
			}

			auto* party_data = game::Lobby_GetPartyData(0);
			if (!party_data)
			{
				party_data = hosted_dedicated_party_state.game_lobby;
			}

			if (!party::session::is_dedicated_lobby(party_data))
			{
				return -1;
			}

			auto count = 0;
			for (auto member_index = 0; member_index < static_cast<int>(std::size(party_data->members)); ++member_index)
			{
				if (game::Party_IsMemberUIVisible(party_data, member_index))
				{
					++count;
				}
			}

			return count;
		}

		bool parse_integer(const std::string& value, const int minimum,
			const int maximum, int& result)
		{
			if (value.empty())
			{
				return false;
			}

			const auto [end, error] = std::from_chars(
				value.data(), value.data() + value.size(), result);
			return error == std::errc{} && end == value.data() + value.size()
				&& result >= minimum && result <= maximum;
		}

		bool parse_match_sequence(const std::string& value, std::uint64_t& result)
		{
			if (value.empty())
			{
				return false;
			}

			const auto [end, error] = std::from_chars(
				value.data(), value.data() + value.size(), result);
			return error == std::errc{} && end == value.data() + value.size()
				&& result != 0;
		}

		void apply_hosted_party_capacity(game::PartyData* party_data, const bool joining = false)
		{
			if (!joining && !party::session::is_dedicated_lobby(party_data))
			{
				return;
			}

			const auto max_players = hosted_dedicated_party_state.max_players;
			if (max_players < 1
				|| max_players > game::environment::get_online_mode_info().max_players)
			{
				return;
			}

			const auto member_capacity = dedicated_party::get_member_capacity(max_players);
			auto apply = [member_capacity](game::PartyData* target)
			{
				if (target)
				{
					game::Party_SetMaxClients(target, member_capacity);
				}
			};

			apply(party_data);
			if (game::Lobby_GetPartyData(0) != party_data)
			{
				apply(game::Lobby_GetPartyData(0));
			}

			auto* private_party = game::Party_GetPrivatePartyData();
			if (private_party != party_data && private_party != game::Lobby_GetPartyData(0))
			{
				apply(private_party);
			}
		}

		bool validate_map_and_gametype(const std::string& map_name, const std::string& gametype)
		{
			if (map_name.empty())
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
			if (!party::resolve_map_index(map_name, map_index))
			{
				console::error("Connection failed: map '%s' is not available locally.\n",
					map_name.data());
				return false;
			}

			return true;
		}

		bool pending_hosted_party_join_matches(const void* session_info)
		{
			if (!hosted_party_join_state.active || !session_info
				|| !party::is_connection_attempt_current(hosted_party_join_state.attempt_id))
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
				|| !party::resolve_map_index(map_name_value, map_index)
				|| !party::validate_gametype(gametype_value))
			{
				return false;
			}

			const auto changed = hosted_dedicated_party_state.map_name != map_name_value
				|| hosted_dedicated_party_state.gametype != gametype_value;
			hosted_dedicated_party_state.map_name = map_name_value;
			hosted_dedicated_party_state.gametype = gametype_value;

			if (apply_settings)
			{
				party::apply_map_settings(map_name_value, gametype_value, map_index);
			}

			if (changed)
			{
				console::info("Hosted dedicated lobby: match updated to %s %s.\n",
					map_name_value.data(), gametype_value.data());
			}

			return true;
		}

		void request_hosted_dedicated_party_sync()
		{
			if (!hosted_dedicated_party_state.sync_after_next_go)
			{
				return;
			}

			// Party state can arrive during post-match results before the host has
			// selected the next rotation entry. Keep this armed until that selection,
			// and let each newer party state supersede an older in-flight query.
			hosted_dedicated_party_state.sync_challenge =
				utils::cryptography::random::get_challenge();
			network::send(hosted_dedicated_party_state.target, "s2x_getInfo",
				hosted_dedicated_party_state.sync_challenge);
		}

		void party_client_process_party_state_stub(game::PartyData* party_data,
			std::uint32_t* active_client, game::netadr_s* from)
		{
			party_client_process_party_state_hook.invoke<void>(
				party_data, active_client, from);

			if (!is_hosted_dedicated_party_address(from)
				|| !party::session::is_dedicated_lobby(party_data))
			{
				return;
			}

			hosted_dedicated_party_state.game_lobby = party_data;
			apply_hosted_party_capacity(party_data);
			refresh_presentation();

			// Public partystate applies its playlist rules after parsing and can replace
			// the dedicated host's free-form map/gametype with a local default. Restore
			// those settings during gameplay and go processing, but leave mapname under
			// native control while it switches from the finished map to the virtual lobby.
			const auto in_virtual_lobby = game::virtual_lobby_loaded();
			if ((!in_virtual_lobby || hosted_dedicated_go_in_progress)
				&& hosted_dedicated_party_state.sync_after_next_go
				&& !hosted_dedicated_party_state.map_name.empty()
				&& !hosted_dedicated_party_state.gametype.empty())
			{
				update_hosted_dedicated_party_match(
					hosted_dedicated_party_state.map_name,
					hosted_dedicated_party_state.gametype,
					game::environment::is_multiplayer());
			}

			if (in_virtual_lobby)
			{
				// Refresh once after a match so the next rotation selection is learned.
				request_hosted_dedicated_party_sync();
			}
		}

		std::int64_t party_client_handle_go_stub(game::PartyData* party_data, void* command_data,
			game::netadr_s* from, game::msg_t* message)
		{
			auto hosted_go = false;
			if (is_hosted_dedicated_party_address(from) && game::Cmd_Argc() > 6)
			{
				// PartyClient_HandleGo passes argv[5] and argv[6] to
				// CL_ConnectAndPreloadMap as the map and gametype respectively.
				const auto* map_name = game::Cmd_Argv(5);
				const auto* gametype = game::Cmd_Argv(6);
				if (map_name && gametype
					&& update_hosted_dedicated_party_match(map_name, gametype, false))
				{
					hosted_go = true;
					hosted_dedicated_party_state.sync_after_next_go = true;
				}
			}

			hosted_dedicated_go_in_progress = hosted_go;
			if (hosted_dedicated_go_in_progress
				&& game::environment::is_zombies())
			{
				// Stock Zombies ready-up otherwise consumes the go command without
				// entering its native preload path. A dedicated go is the server's
				// readiness decision, so confirm it through the stock setter.
				// Listen lobbies keep the normal Ready Up/loadout-selection flow.
				const auto controller_index =
					game::CL_ControllerIndexFromClientNum(0);
				game::PartyClient_SetLocalReadyUpFlag(controller_index);
			}

			const auto result = party_client_handle_go_hook.invoke<std::int64_t>(
				party_data, command_data, from, message);
			hosted_dedicated_go_in_progress = false;

			// HandleGo can ignore or defer a message while the virtual lobby loads.
			// Only CL_ConnectAndPreloadMap may commit the accepted match's settings;
			// writing mapname here can make lobby startup request the next map's BSP.
			return result;
		}

		void cl_connect_and_preload_map_stub(const int local_client_num, void* session_info,
			game::netadr_s* target, const char* map_name, const char* gametype)
		{
			if (game::environment::is_multiplayer()
				&& hosted_dedicated_go_in_progress && map_name && gametype)
			{
				// Public PartyClient_HandleGo runs Playlist_RunRules before this call.
				// Restore the map/gametype carried by the go command at the last native
				// boundary before client gameplay memory and UI state are selected.
				update_hosted_dedicated_party_match(map_name, gametype, true);
			}

			cl_connect_and_preload_map_hook.invoke<void>(
				local_client_num, session_info, target, map_name, gametype);
		}

		int party_atomic_setup_potential_host_stub(const int controller_index,
			const void* session_info, const int party_type, const int max_players,
			const int a5, const int a6, game::PartyAtomicJoinInfo* join_info)
		{
			// HQ must use party type 2. Keep the existing game-session path:
			// native CL_Connect also uses type 3 before resolving the game party.
			const auto is_hosted_party_join = pending_hosted_party_join_matches(session_info)
				&& (hosted_party_join_state.kind != party::session::kind::headquarters
					|| party_type == headquarters::party_type);
			// Own the request across the native call and pending-state cleanup below.
			const auto pending_join = hosted_party_join_state;
			const auto dedicated_join = is_hosted_party_join
				&& pending_join.kind == party::session::kind::dedicated;
			auto setup_member_capacity = max_players;
			if (dedicated_join)
			{
				setup_member_capacity = dedicated_party::get_session_capacity();
			}
			else if (is_hosted_party_join && pending_join.kind == party::session::kind::headquarters)
			{
				// Native pa_joinhub passes eight (MP RVA 0x4973FB). Override only a
				// validated S2x HQ join, including the temporary-session fallback below.
				setup_member_capacity = headquarters::session_capacity;
			}

			const auto result = party_atomic_setup_potential_host_hook.invoke<int>(
				controller_index, session_info, party_type, setup_member_capacity, a5, a6, join_info);

			if (!is_hosted_party_join)
			{
				return result;
			}

			if (!party::is_connection_attempt_current(pending_join.attempt_id))
			{
				return result;
			}

			if (hosted_party_join_state.attempt_id == pending_join.attempt_id
				&& hosted_party_join_state.session_id == pending_join.session_id)
			{
				hosted_party_join_state = {};
			}

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
					session_info, 0, setup_member_capacity, a5))
				{
					console::error("Hosted dedicated lobby: native party session setup failed.\n");
					return false;
				}
			}

			join_info->address = pending_join.target;
			join_info->addressValid = 1;
			hosted_dedicated_party_state = {};
			joined_custom_session_id.clear();
			joined_hq_session_id.clear();
			if (pending_join.kind == party::session::kind::headquarters)
			{
				// Retain the validated session identity for owner filtering. HQ still
				// owns its lifecycle; skip game-party presentation and rotation.
				joined_hq_session_id = pending_join.session_id;
				console::info("[HQ] Joining native HQ session through %s.\n",
					network::net_adr_to_string(pending_join.target));
				return true;
			}

			if (!dedicated_join)
			{
				// A listen host is a real player. No owner filtering, extra session
				// slot, public-playlist overrides, or dedicated rotation sync here.
				joined_custom_session_id = pending_join.session_id;
				console::info("Custom match lobby: joining through %s.\n",
					network::net_adr_to_string(pending_join.target));
				return true;
			}
			hosted_dedicated_party_state.target = pending_join.target;
			hosted_dedicated_party_state.session_id = pending_join.session_id;
			hosted_dedicated_party_state.match_sequence = pending_join.match_sequence;
			hosted_dedicated_party_state.map_name = pending_join.map_name;
			hosted_dedicated_party_state.gametype = pending_join.gametype;
			hosted_dedicated_party_state.game_lobby = game::Lobby_GetPartyData(0);
			hosted_dedicated_party_state.max_players = pending_join.max_players;
			hosted_dedicated_party_state.sync_after_next_go = true;
			apply_hosted_party_capacity(hosted_dedicated_party_state.game_lobby, true);
			refresh_presentation();

			console::info("Hosted dedicated lobby: joining through %s.\n",
				network::net_adr_to_string(pending_join.target));
			return true;
		}

		bool prepare_headquarters_join(bool& preparation_started)
		{
			if (party::loaded_map_name() == headquarters::map_name && game::CL_IsLocalClientInGame(0))
			{
				return true;
			}

			if (!preparation_started)
			{
				preparation_started = true;
				console::info("[HQ] Loading full HQ before the native session join.\n");
				// SV_VirtualLobby_f's fullhub branch (0x6D8CD0 -> 0x484130).
				// Wait for CA_ACTIVE before joining: local HQ startup otherwise races
				// the remote gamestate and can replace the session during loading.
				utils::hook::invoke<void>(0x484130_g);
			}

			return false;
		}

		void join_headquarters(const party::internal_connect_request& request)
		{
			// CL_Connect (0x6D0A0) parses id at +0, 37-byte bdCommonAddr at +8,
			// and the 16-byte key at +45. The HQ join copies 68 bytes
			// (0x471E40 / 0x497EF0); preserve alignment and zero the trailing bytes.
			constexpr auto address_offset = 8;
			constexpr auto key_offset = 45;
			alignas(16) std::array<std::uint8_t, 68> descriptor{};
			const auto id = utils::hook::invoke<std::uint64_t>(0x66EAF0_g, request.session.session_id.data());
			std::memcpy(descriptor.data(), &id, sizeof(id));
			utils::hook::invoke<void>(0x66EB40_g, request.session.host_address.data(), descriptor.data() + address_offset);
			utils::hook::invoke<void>(0x66EA70_g, request.session.key.data(), descriptor.data() + key_offset);

			// Mirror pa_joinhub: select full HQ, then enter the native HQ atomic join.
			utils::hook::invoke<void>(0x483D50_g);
			utils::hook::invoke<void>(0x497290_g,
				game::CL_ControllerIndexFromClientNum(0), descriptor.data());
			utils::hook::invoke<void>(0xA74A50_g, descriptor.data() + key_offset);
			utils::hook::invoke<void>(0xA74AA0_g, descriptor.data());
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

			lobby["GetDedicatedPartyMapName"] = []
			{
				return get_map_name();
			};

			lobby["GetDedicatedPartyGameType"] = []
			{
				return get_gametype();
			};

			lobby["GetDedicatedPartyMemberCount"] = []
			{
				return get_hosted_dedicated_party_member_count();
			};

			lobby["GetDedicatedPartyMaxPlayers"] = []
			{
				return party::session::get_dedicated_max_players();
			};

			refresh_presentation();
		}
	}

	bool try_handle_join(const game::netadr_s& from, const utils::info_string& info,
		const int max_players, const std::uint64_t attempt_id)
	{
		const auto kind = party::session::classify(info.get("party_session"));
		if (kind == party::session::kind::none)
		{
			return false;
		}

		const auto is_hq_join = kind == party::session::kind::headquarters;
		if (kind == party::session::kind::invalid
			|| (is_hq_join
				&& (!game::environment::is_multiplayer() || game::environment::is_dedicated()))
			|| (kind == party::session::kind::custom && !game::environment::uses_multiplayer_binary()))
		{
			console::error("Connection failed: invalid hosted-party type.\n");
			return true;
		}

		const party::session::descriptor session{
			info.get("session_host"), info.get("session_key"), info.get("session_id")
		};
		const auto map_name = info.get("party_mapname");
		const auto gametype = info.get("party_gametype");
		std::uint64_t match_sequence{};

		if (!party::session::valid_descriptor(session)
			|| max_players < 1
			|| max_players > party::session::get_player_limit(kind)
			|| (is_hq_join
				&& (max_players != headquarters::player_capacity
					|| map_name != headquarters::map_name || gametype != headquarters::gametype))
			|| (kind == party::session::kind::dedicated
				&& !parse_match_sequence(info.get("party_match_sequence"), match_sequence))
			|| !validate_map_and_gametype(map_name, gametype)
			|| !party::validate_gametype(gametype))
		{
			console::error("Connection failed: invalid hosted-party session data.\n");
			return true;
		}

		console::info("Joining %s lobby on map '%s' gametype '%s'.\n",
			is_hq_join ? "HQ"
				: kind == party::session::kind::custom ? "custom match" : "hosted dedicated",
			map_name.data(), gametype.data());

		if (!party::is_connection_attempt_current(attempt_id))
		{
			return true;
		}

		cancel_pending_connection();

		auto target = from;
		target.localNetID = game::NS_SERVER;

		const auto generation = pending_join_generation;
		const auto deadline = std::chrono::steady_clock::now() + hq_preparation_timeout;
		const party::internal_connect_request request{ attempt_id, session, map_name, gametype };
		scheduler::schedule([request, target, max_players, match_sequence, kind,
			generation, deadline, preparing_hq = false]() mutable
		{
			if (!party::is_connection_attempt_current(request.attempt_id)
				|| generation != pending_join_generation)
			{
				return scheduler::cond_end;
			}

			if (kind == party::session::kind::headquarters)
			{
				if (!game::environment::is_multiplayer() || game::is_local_play()
					|| (!*game::virtualLobby_Loaded && !*game::virtualLobby_Requested))
				{
					console::error("[HQ] Enter the online Multiplayer HQ before joining.\n");
					return scheduler::cond_end;
				}

				if (std::chrono::steady_clock::now() >= deadline)
				{
					console::error("[HQ] Full HQ preparation timed out; join cancelled.\n");
					return scheduler::cond_end;
				}

				if (!prepare_headquarters_join(preparing_hq))
				{
					return scheduler::cond_continue;
				}
			}

			// The potential-host hook consumes this state during the native join.
			hosted_party_join_state = {
				true, kind, request.attempt_id, match_sequence, target,
				request.session.session_id, request.map_name, request.gametype, max_players
			};
			if (kind == party::session::kind::headquarters)
			{
				join_headquarters(request);
				cancel_pending_connection();
			}
			else
			{
				// CL_Connect parses the stock JoinServer descriptor for game sessions.
				party::execute_internal_connect(request);
			}

			return scheduler::cond_end;
		}, scheduler::pipeline::main);

		return true;
	}

	bool is_pending_internal_connect(const std::string_view session_id,
		const std::uint64_t attempt_id)
	{
		return hosted_party_join_state.active
			&& hosted_party_join_state.attempt_id == attempt_id
			&& hosted_party_join_state.session_id == session_id
			&& party::is_connection_attempt_current(attempt_id);
	}

	bool try_handle_sync_response(const game::netadr_s& from, const utils::info_string& info,
		const std::string& challenge)
	{
		if (!is_hosted_dedicated_party_address(&from)
			|| hosted_dedicated_party_state.sync_challenge.empty()
			|| challenge != hosted_dedicated_party_state.sync_challenge)
		{
			return false;
		}

		int protocol{};
		if (!parse_integer(info.get("protocol"), 0,
			std::numeric_limits<int>::max(), protocol) || protocol != PROTOCOL)
		{
			console::error("Connection failed: invalid protocol.\n");
			hosted_dedicated_party_state.sync_challenge.clear();
			return true;
		}

		const auto gamename = info.get("gamename");
		if (gamename != "S2")
		{
			console::error("Connection failed: invalid gamename '%s'.\n", gamename.data());
			hosted_dedicated_party_state.sync_challenge.clear();
			return true;
		}

		const auto& mode = game::environment::get_online_mode_info();
		const auto server_mode = info.get("mode");
		if (server_mode != mode.token)
		{
			console::error(
				"Hosted dedicated lobby: server mode '%s' does not match client mode '%s'.\n",
				server_mode.empty() ? "<missing>" : server_mode.data(),
				mode.token.data());
			hosted_dedicated_party_state.sync_challenge.clear();
			return true;
		}

		int max_players{};
		if (!parse_integer(info.get("sv_maxclients"), 1,
			mode.max_players, max_players))
		{
			console::error("Hosted dedicated lobby: invalid party capacity.\n");
			hosted_dedicated_party_state.sync_challenge.clear();
			return true;
		}

		std::uint64_t match_sequence{};
		if (!parse_match_sequence(info.get("party_match_sequence"), match_sequence))
		{
			console::error("Hosted dedicated lobby: invalid match sequence.\n");
			hosted_dedicated_party_state.sync_challenge.clear();
			return true;
		}

		hosted_dedicated_party_state.sync_challenge.clear();
		if (party::session::classify(info.get("party_session")) == party::session::kind::dedicated
			&& info.get("session_id") == hosted_dedicated_party_state.session_id
			&& party::session::is_dedicated_lobby(game::Lobby_GetPartyData(0)))
		{
			hosted_dedicated_party_state.max_players = max_players;
			apply_hosted_party_capacity(hosted_dedicated_party_state.game_lobby);
			// Keep the next rotation entry out of live map dvars while the current map
			// is unloading. CL_ConnectAndPreloadMap commits it when the server sends go.
			if (match_sequence > hosted_dedicated_party_state.match_sequence
				&& update_hosted_dedicated_party_match(
					info.get("party_mapname"), info.get("party_gametype"), false))
			{
				hosted_dedicated_party_state.match_sequence = match_sequence;
				hosted_dedicated_party_state.sync_after_next_go = false;
			}
			refresh_presentation();
		}

		return true;
	}

	bool has_dedicated_session(game::PartyData* party_data)
	{
		return has_joined_session(party_data, 0, hosted_dedicated_party_state.session_id);
	}

	bool has_headquarters_session(game::PartyData* party_data)
	{
		return has_joined_session(party_data, headquarters::party_type, joined_hq_session_id);
	}

	int get_joined_max_players()
	{
		return hosted_dedicated_party_state.max_players;
	}

	std::string get_map_name()
	{
		if (game::environment::is_dedicated())
		{
			return party::loaded_map_name();
		}

		return party::session::is_dedicated_lobby(game::Lobby_GetPartyData(0))
			? hosted_dedicated_party_state.map_name : std::string{};
	}

	std::string get_gametype()
	{
		if (game::environment::is_dedicated())
		{
			return dedicated_party::get_current_gametype();
		}

		return party::session::is_dedicated_lobby(game::Lobby_GetPartyData(0))
			? hosted_dedicated_party_state.gametype : std::string{};
	}

	void refresh_presentation()
	{
		scheduler::once([]
		{
			if (!*game::hks::lui_lua_state)
			{
				return;
			}

			game::LUI_EnterCriticalSection();
			try
			{
				const auto refresh = ui_scripting::get_globals().get(
					"S2xRefreshDedicatedPartyPresentation");
				if (refresh.is<ui_scripting::function>())
				{
					refresh.as<ui_scripting::function>()();
				}
			}
			catch (const std::exception& e)
			{
				console::error("Hosted dedicated lobby: presentation refresh failed: %s\n",
					e.what());
			}
			game::LUI_LeaveCriticalSection();
		}, scheduler::pipeline::main);
	}

	void cancel_pending_connection()
	{
		++pending_join_generation;
		hosted_party_join_state = {};
		hosted_dedicated_go_in_progress = false;
	}

	void commit_direct_connection()
	{
		reset();
	}

	void reset()
	{
		cancel_pending_connection();
		hosted_dedicated_party_state = {};
		joined_custom_session_id.clear();
		joined_hq_session_id.clear();
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			ui_scripting::on_start(install_lobby_functions);

			if (game::environment::is_dedicated())
			{
				return;
			}

			cl_connect_and_preload_map_hook.create(
				game::CL_ConnectAndPreloadMap, cl_connect_and_preload_map_stub);
			party_atomic_setup_potential_host_hook.create(
				0x497EF0_g, party_atomic_setup_potential_host_stub);

			utils::hook::call(0x497767_g, party_atomic_activate_lobby_stub);

			// The kickedFromLobby handler hardcodes the VAC-ban text; show the stock party-kick text instead.
			utils::hook::inject(0x47223E_g + 3, 0xB72C68_g);
			utils::hook::inject(0x472287_g + 3, 0xB72C68_g);
			party_client_kicked_from_lobby_hook.create(0x4721C0_g, party_client_kicked_from_lobby_stub);
			utils::hook::call(0x47228E_g, show_lobby_kick_message_stub);

			party_client_handle_go_hook.create(
				game::PartyClient_HandleGo, party_client_handle_go_stub);
			party_client_process_party_state_hook.create(
				game::PartyClient_ProcessPartyState, party_client_process_party_state_stub);
		}
	};
}

REGISTER_COMPONENT(dedicated_party_client::component)
