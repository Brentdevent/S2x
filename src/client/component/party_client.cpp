#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "party_client.hpp"
#include "arena.hpp"
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
#include <utils/finally.hpp>
#include <utils/hook.hpp>
#include <utils/info_string.hpp>
#include <utils/string.hpp>

#include <charconv>

namespace party_client
{
	namespace
	{
		constexpr auto hq_preparation_timeout = 120s;

		utils::hook::detour quit_hook;
		utils::hook::detour party_client_host_presence_hook;
		utils::hook::detour cl_connect_and_preload_map_hook;
		utils::hook::detour party_atomic_setup_potential_host_hook;
		utils::hook::detour party_atomic_request_join_hook;
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
			bool friend_join{};
			std::uint32_t discord_token{};
			bool custom_invite{};
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
		std::string joined_social_session_id{};
		game::netadr_s joined_game_target{};
		game::netadr_s joined_hq_target{};

		struct pending_party_follow
		{
			std::uint64_t attempt_id{};
			std::uint64_t private_session{};
			std::uint64_t game_session{};
			std::chrono::steady_clock::time_point expires;
			int sequence{};
			int party_type{};
			bool remote_game{};
			game::PartySessionInfo session_info{};
			game::netadr_s leader{};
			std::array<std::uint32_t, 2> active_client{};
			game::msg_t message{};
			std::string data;
		};

		std::optional<pending_party_follow> party_follow;

		bool hosted_dedicated_go_in_progress{};
		thread_local bool preserve_local_players_for_join{};
		bool custom_invite_token{};

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
				|| has_joined_session(party_data, 0, joined_custom_session_id)
				|| has_joined_session(party_data, 1, joined_social_session_id);
			
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

		void party_atomic_request_join_stub(game::PartyAtomicJoinInfo* join_info,
			const int controller_index, const void* session_info)
		{
			// SetupPotentialHost consumes hosted_party_join_state inside the native call.
			const auto custom_join = pending_hosted_party_join_matches(session_info)
				&& hosted_party_join_state.kind == party::session::kind::custom;
			const auto friend_join = custom_join && hosted_party_join_state.friend_join;

			if (custom_join)
			{
				// Retail Discord sets this before RequestJoin (84DB8D). The native
				// join state machine clears it on completion, failure and timeout.
				const auto token = friend_join ? 0 : hosted_party_join_state.discord_token;
				utils::hook::invoke<void>(0x1E4990_g, token);
				custom_invite_token = token != 0;
			}

			const auto previous = preserve_local_players_for_join;
			const auto restore = utils::finally([previous]
			{
				preserve_local_players_for_join = previous;
			});

			preserve_local_players_for_join = pending_hosted_party_join_matches(session_info)
				&& (hosted_party_join_state.kind == party::session::kind::dedicated
					|| hosted_party_join_state.kind == party::session::kind::custom)
				&& utils::hook::invoke<bool>(0x4A1EB0_g, 0) // CL_IsLocalClientActive
				&& utils::hook::invoke<bool>(0x4A1EB0_g, 1);

			if (preserve_local_players_for_join)
			{
				console::info("Hosted lobby: preserving both local players during the join.\n");
			}

			party_atomic_request_join_hook.invoke<void>(join_info, controller_index, session_info);

			if (friend_join && join_info)
			{
				// 497D8E writes PJT_INVITE (2); 4968F8 serializes this field.
				// Steam must use PJT_PRESENCE (1), which checks the host's invite list.
				join_info->joinType = 1;
			}
		}

		bool keep_social_party()
		{
			auto* private_party = game::Party_GetPrivatePartyData();
			return hosted_party_join_state.active && !hosted_party_join_state.custom_invite
				&& (hosted_party_join_state.kind == party::session::kind::dedicated
					|| hosted_party_join_state.kind == party::session::kind::custom)
				&& party::is_connection_attempt_current(hosted_party_join_state.attempt_id)
				&& private_party && utils::hook::invoke<bool>(0x471200_g, private_party)
				&& game::Party_AreWeHost(private_party);
		}

		void stop_private_party_for_join(game::PartyData* party_data)
		{
			if (!keep_social_party())
			{
				utils::hook::invoke<void>(0x4843B0_g, party_data);
			}
		}

		void clear_private_session_for_join(game::SessionData* session)
		{
			if (!keep_social_party())
			{
				utils::hook::invoke<void>(0x6FD220_g, session);
			}
		}

		bool party_join_uses_local_controllers_stub()
		{
			return preserve_local_players_for_join || utils::hook::invoke<bool>(0x856770_g);
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
			arena::map_registration_scope registration;
			if (map_name_value.empty() || gametype_value.empty()
				|| !party::resolve_map_index(map_name_value, map_index)
				|| !party::validate_gametype(gametype_value))
			{
				return false;
			}

			registration.keep();

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
			joined_hq_target = {};
			if (pending_join.kind == party::session::kind::headquarters)
			{
				// Retain the validated session identity for owner filtering. HQ still
				// owns its lifecycle; skip game-party presentation and rotation.
				joined_hq_session_id = pending_join.session_id;
				joined_hq_target = pending_join.target;
				console::info("[HQ] Joining %s...\n",
					network::net_adr_to_string(pending_join.target));
				return true;
			}

			if (pending_join.kind == party::session::kind::social)
			{
				joined_social_session_id = pending_join.session_id;
				console::info("[party] Joining social party through %s.\n",
					network::net_adr_to_string(pending_join.target));
				return true;
			}

			joined_game_target = pending_join.target;

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

		game::PartyData* get_party_follow_destination()
		{
			auto* game_party = game::Lobby_GetPartyData(0);
			if (game_party && utils::hook::invoke<bool>(0x471200_g, game_party)
				&& (game::Party_AreWeHost(game_party) || has_dedicated_session(game_party)
					|| has_joined_session(game_party, 0, joined_custom_session_id)))
			{
				return game_party;
			}

			auto* hq_party = game::Lobby_GetPartyData(headquarters::party_type);
			return game::environment::is_multiplayer() && has_headquarters_session(hq_party)
				&& utils::hook::invoke<bool>(0x471200_g, hq_party) ? hq_party : nullptr;
		}

		void write_party_follow_session(game::msg_t* message, const void* data, const int size)
		{
			if (size != sizeof(game::PartySessionInfo) || game::is_local_play())
			{
				utils::hook::invoke<void>(0xDDAF0_g, message, data, size);
				return;
			}

			game::PartySessionInfo descriptor{};
			std::memcpy(&descriptor, data, sizeof(descriptor));
			auto* game_party = get_party_follow_destination();
			const auto owned = game_party && game_party->session
				&& descriptor.id == game_party->session->sessionId;
			const auto hosted = owned && game::Party_AreWeHost(game_party);
			const auto hq = owned && has_headquarters_session(game_party);
			const auto joined = owned && (has_dedicated_session(game_party)
				|| has_joined_session(game_party, 0, joined_custom_session_id) || hq);

			if (hosted || joined)
			{
				// bdCommonAddr serializes five local IPv4/port pairs, then its public
				// pair (A78470 / A75D30). Mark the descriptor's unused alignment bytes
				// so only S2x party members interpret this raw address. A zero pair
				// means the current leader, whose reachable endpoint is already known.
				const auto& target = hq ? joined_hq_target : joined_game_target;
				const std::uint32_t address = joined ? target.addr : 0;
				const std::uint16_t port = joined ? ntohs(target.port) : 0;
				std::memcpy(descriptor.address + 30, &address, sizeof(address));
				std::memcpy(descriptor.address + 34, &port, sizeof(port));
				std::memcpy(descriptor.padding, "S2X", sizeof(descriptor.padding));
			}

			utils::hook::invoke<void>(0xDDAF0_g, message, &descriptor, size);
		}

		int party_atomic_is_joining_follow_session_stub(void* join_info, const game::PartySessionInfo* session_info)
		{
			if (std::memcmp(session_info->padding, "S2X", sizeof(session_info->padding)) == 0)
			{
				std::array<char, 17> session_id{};
				game::Session_IdToString(session_info->id, session_id.data());
				if (party::has_session(session_id.data()))
				{
					// HostPresence's completed-session checks compare the descriptor's
					// address to an ID (471676 / 471688). Treat the current S2x session
					// like a pending join so retries only update native presence state.
					return 1;
				}
			}

			return utils::hook::invoke<int>(0x4971A0_g, join_info, session_info);
		}

		void party_client_host_presence_stub(game::PartyData* party_data, const std::uint32_t* active_client,
			game::netadr_s* from, game::msg_t* message)
		{
			// The native handler verifies the source again when the query completes.
			// Do not query an address supplied by anyone except our current leader.
			if (game::is_local_play() || !party_data || party_data != game::Party_GetPrivatePartyData()
				|| !party_data->session || !utils::hook::invoke<bool>(0x471200_g, party_data)
				|| game::Party_AreWeHost(party_data)
				|| !utils::hook::invoke<bool>(0x479490_g, party_data, from)
				|| !utils::hook::invoke<bool>(0x481230_g, party_data, party_data->hostIndex)
				|| message->overflowed || message->splitSize || !message->data
				|| message->cursize < 0 || message->cursize > 1024
				|| message->readcount < 0 || message->readcount > message->cursize
				|| message->bit < 0 || message->bit > message->cursize * 8)
			{
				party_client_host_presence_hook.invoke<void>(party_data, active_client, from, message);
				return;
			}

			auto reader = *message;
			const auto type = utils::hook::invoke<int>(0xDCB40_g, &reader);
			utils::hook::invoke<int>(0xDCB40_g, &reader); // Native mode.
			const auto sequence = utils::hook::invoke<int>(0xDC910_g, &reader, 8);
			game::PartySessionInfo descriptor{};
			if (type == 0 || type == headquarters::party_type)
			{
				utils::hook::invoke<void>(0xDCBB0_g, &reader, sizeof(descriptor), &descriptor, sizeof(descriptor));
				utils::hook::invoke<int>(0xDCB40_g, &reader); // Capacity.
				utils::hook::invoke<int>(0xDCB40_g, &reader); // Private match.
				utils::hook::invoke<int>(0xDC870_g, &reader); // Native join flag.
			}

			if ((type != 0 && type != headquarters::party_type)
				|| (type == headquarters::party_type && !game::environment::is_multiplayer())
				|| reader.overflowed || std::memcmp(descriptor.padding, "S2X", 3) != 0)
			{
				if (type == 1 && !reader.overflowed && party_follow
					&& is_party_follow_current(party_follow->attempt_id)
					&& (sequence > party_follow->sequence
						|| (sequence != party_follow->sequence && party_follow->sequence > 170 && sequence < 85)))
				{
					// A newer leader backout cancels a query still waiting for the old game.
					party::cancel_pending_connection();
				}

				party_client_host_presence_hook.invoke<void>(party_data, active_client, from, message);
				return;
			}

			const auto previous = party_follow && is_party_follow_current(party_follow->attempt_id)
				? party_follow->sequence : static_cast<int>(party_data->presenceSequence);
			if (!(sequence > previous || (sequence != previous && previous > 170 && sequence < 85)))
			{
				return;
			}

			game::netadr_s target = *from;
			if (target.type == game::NA_BROADCAST)
			{
				// Native reliable party packets use this type for raw IP peers too.
				target.type = game::NA_IP;
			}

			std::uint32_t address{};
			std::uint16_t port{};
			std::memcpy(&address, descriptor.address + 30, sizeof(address));
			std::memcpy(&port, descriptor.address + 34, sizeof(port));
			if (address || port)
			{
				if (!address || !port)
				{
					return;
				}

				target.type = game::NA_IP;
				target.addr = address;
				target.port = htons(port);
			}

			std::array<char, 17> session_id{};
			game::Session_IdToString(descriptor.id, session_id.data());
			if (party::has_session(session_id.data())
				|| utils::hook::invoke<bool>(0x4971A0_g,
					utils::hook::invoke<void*>(0x47D270_g, type), &descriptor)
				|| (!game::virtual_lobby_loaded() && game::CL_IsLocalClientInGame(0)))
			{
				// Let stock presence acknowledge an existing join and handle gameplay
				// backout. Never discard the social party through an invite disconnect.
				party_client_host_presence_hook.invoke<void>(party_data, active_client, from, message);
				return;
			}

			if (!descriptor.id || target.type != game::NA_IP || !target.port
				|| (party_follow && is_party_follow_current(party_follow->attempt_id)
					&& party_follow->game_session == descriptor.id))
			{
				return;
			}

			pending_party_follow pending{};
			pending.private_session = party_data->session->sessionId;
			pending.game_session = descriptor.id;
			pending.expires = std::chrono::steady_clock::now() + 90s;
			pending.sequence = sequence;
			pending.party_type = type;
			pending.remote_game = address != 0;
			pending.session_info = descriptor;
			pending.leader = *from;
			std::copy_n(active_client, pending.active_client.size(), pending.active_client.begin());
			pending.message = *message;
			pending.data.assign(message->data, message->cursize);
			const auto now = std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			party::queue_session_join(network::net_adr_to_string(target), session_id.data(), now + 90, false, 0, true);
			pending.attempt_id = party::get_connection_attempt_id();
			party_follow = std::move(pending);
		}

		void join_headquarters(game::PartySessionInfo& descriptor)
		{
			// Mirror pa_joinhub (471E40): select full HQ and enter its atomic join.
			utils::hook::invoke<void>(0x483D50_g);
			utils::hook::invoke<void>(0x497290_g,
				game::CL_ControllerIndexFromClientNum(0), &descriptor);
			utils::hook::invoke<void>(0xA74A50_g, descriptor.key);
			utils::hook::invoke<void>(0xA74AA0_g, &descriptor);
		}

		void complete_party_follow(const party::internal_connect_request& request)
		{
			if (!is_party_follow_current(request.attempt_id))
			{
				cancel_pending_connection();
				return;
			}

			auto pending = std::move(*party_follow);
			party_follow.reset();
			pending.message.data = pending.data.data();
			pending.message.splitData = nullptr;
			if (pending.party_type == headquarters::party_type)
			{
				// Native hostPresence ignores HQ follows while we own the local HQ
				// (4714FD). The normal HQ join retires it first. Its descriptor must
				// match the presence so native retries recognize the pending join.
				join_headquarters(pending.session_info);
			}

			// SetupPotentialHost consumes the validated session kind inside this call.
			const auto kind = hosted_party_join_state.kind;

			// Native hostPresence owns the sequence, controller selection, backout
			// and PJT_FOLLOW flow. SetupPotentialHost uses the validated query result.
			party_client_host_presence_hook.invoke<void>(game::Party_GetPrivatePartyData(),
				pending.active_client.data(), &pending.leader, &pending.message);
			hosted_party_join_state = {};

			if (pending.remote_game && pending.party_type == 0)
			{
				auto* join_info = utils::hook::invoke<game::PartyAtomicJoinInfo*>(0x47D270_g, 0);
				if (join_info->state == 11 && join_info->joinType == 3)
				{
					// A remote host has not reserved this follower's slot. Match direct
					// RequestJoin admission for dedicated servers; Custom Matches keep
					// presence admission and its privacy checks. Retain the social party
					// with the local-player counts from RequestJoin (497D7A-497DAE).
					join_info->memberCount = join_info->hasSecondaryLocalClient ? 2 : 1;
					join_info->subPartyCount = 1;
					join_info->subPartySizes[0] = join_info->memberCount;
					join_info->joinType = kind == party::session::kind::dedicated ? 2 : 1;
					utils::hook::invoke<void>(0x497E80_g, join_info, 1, 0);
				}
			}
		}

		bool has_game_party_member(const game::PartyData* game_party, const std::uint64_t xuid)
		{
			return std::any_of(std::begin(game_party->members), std::end(game_party->members),
				[xuid](const auto& member)
				{
					// Native PartyMember_IsPresent (481360). Reserved slots are not joins.
					return member.state >= 5 && member.xuid == xuid;
				});
		}

		std::int8_t find_follow_reservation_stub(game::PartyData* game_party, game::netadr_s* address)
		{
			const auto member = utils::hook::invoke<std::int8_t>(0x47C1A0_g, game_party, address);
			auto* private_party = game::Party_GetPrivatePartyData();
			if (member >= 0 || game::is_local_play() || game_party != game::Lobby_GetPartyData(0)
				|| !private_party || !private_party->session || !game::Party_AreWeHost(private_party))
			{
				return member;
			}

			// Native address lookup ignores a reservation until its session member
			// is active. Match its XUID through the current social-party address.
			const auto social_member = utils::hook::invoke<std::int8_t>(0x47C1A0_g, private_party, address);
			if (social_member < 0 || private_party->members[social_member].state < 5)
			{
				return member;
			}

			for (auto slot = 0; slot < static_cast<int>(std::size(game_party->members)); ++slot)
			{
				const auto& reserved = game_party->members[slot];
				if (reserved.state != 0 && reserved.xuid == private_party->members[social_member].xuid)
				{
					return static_cast<std::int8_t>(slot);
				}
			}

			return member;
		}

		bool reserve_party_follow_slots(game::PartyData* private_party, game::PartyData* game_party)
		{
			auto needed = 0;
			for (const auto& member : private_party->members)
			{
				if (member.state >= 5 && std::none_of(std::begin(game_party->members), std::end(game_party->members),
					[&member](const auto& reserved) { return reserved.state != 0 && reserved.xuid == member.xuid; }))
				{
					++needed;
				}
			}

			if (!needed)
			{
				return true;
			}

			const auto capacity = std::min<std::size_t>(std::size(game_party->members),
				static_cast<std::size_t>(game_party->session->publicSlots) + game_party->session->privateSlots);
			const auto available = std::count_if(game_party->members, game_party->members + capacity,
				[](const auto& member) { return member.state == 0; });
			if (needed > available)
			{
				// The native reservation routine raises Com_Error when full. Wait for
				// space instead of dropping the host from a scheduler callback.
				return false;
			}

			utils::hook::invoke<void>(0x4896D0_g, game_party, game::CL_ControllerIndexFromClientNum(0));
			return true;
		}

		bool send_party_follow(game::PartyData* private_party, game::PartyData* game_party)
		{
			const auto hosting = game::Party_AreWeHost(game_party);
			const auto hq = has_headquarters_session(game_party);
			if (!hosting && !has_dedicated_session(game_party)
				&& !has_joined_session(game_party, 0, joined_custom_session_id) && !hq)
			{
				return false;
			}

			std::vector<int> followers;
			for (auto member = 0; member < static_cast<int>(std::size(private_party->members)); ++member)
			{
				if (game::Party_IsMemberUIVisible(private_party, member)
					&& !game::Party_IsMemberLocalPlayer(private_party, member)
					&& !has_game_party_member(game_party, private_party->members[member].xuid))
				{
					followers.push_back(member);
				}
			}

			if (followers.empty())
			{
				return false;
			}

			if (hosting && !reserve_party_follow_slots(private_party, game_party))
			{
				// Existing reservations still need presence retries while others wait for space.
				std::erase_if(followers, [&](const int member)
				{
					const auto xuid = private_party->members[member].xuid;
					return std::none_of(std::begin(game_party->members), std::end(game_party->members),
						[xuid](const auto& reserved) { return reserved.state != 0 && reserved.xuid == xuid; });
				});

				if (followers.empty())
				{
					return true; // Keep retrying while the lobby is full.
				}
			}

			// Match 4896D0's wire format without reserving slots in a remote roster.
			// A follower still in gameplay consumes presence just to back out; a
			// later sequence lets it join after the frontend has finished loading.
			std::array<char, 128> data{};
			game::msg_t message{};
			utils::hook::invoke<void>(0xDC720_g, &message, data.data(), static_cast<int>(data.size()));
			utils::hook::invoke<void>(0xDDE70_g, &message, "1pa_presence");
			utils::hook::invoke<void>(0xDDAD0_g, &message, hq ? headquarters::party_type : 0);
			utils::hook::invoke<void>(0xDDAD0_g, &message, utils::hook::invoke<int>(0x76BF30_g));
			private_party->presenceSequence = (private_party->presenceSequence + 1) % 255;
			utils::hook::invoke<void>(0xDD8B0_g, &message, private_party->presenceSequence, 8);
			write_party_follow_session(&message, &game_party->session->sessionId, sizeof(game::PartySessionInfo));
			utils::hook::invoke<void>(0xDDAD0_g, &message, game_party->session->publicSlots);
			utils::hook::invoke<void>(0xDDAD0_g, &message, game_party->session->privateSlots);
			utils::hook::invoke<void>(0xDDA40_g, &message,
				utils::hook::invoke<bool>(0x827770_g, game_party->session));

			for (const auto member : followers)
			{
				auto address = private_party->session->members[member].address;
				utils::hook::invoke<void>(0x76DFB0_g, &address, &message, 2, nullptr);
			}

			return true;
		}

		void notify_party_members()
		{
			static std::vector<std::uint64_t> notified;
			static std::chrono::steady_clock::time_point retry_until;
			static std::chrono::steady_clock::time_point next_notification;
			auto* private_party = game::Party_GetPrivatePartyData();
			auto* game_party = get_party_follow_destination();
			if (!private_party || !private_party->session || !game_party || !game_party->session
				|| game::is_local_play() || !utils::hook::invoke<bool>(0x471200_g, private_party)
				|| !game::Party_AreWeHost(private_party) || !utils::hook::invoke<bool>(0x471200_g, game_party)
				|| !private_party->session->sessionId || !game_party->session->sessionId
				|| (game::Party_AreWeHost(game_party) && game_party->hostState != 4 && !game::is_server_running()))
			{
				notified.clear();
				return;
			}

			std::vector<std::uint64_t> members{private_party->session->sessionId, game_party->session->sessionId};
			for (int member = 0; member < static_cast<int>(std::size(private_party->members)); ++member)
			{
				if (game::Party_IsMemberUIVisible(private_party, member))
				{
					members.push_back(private_party->members[member].xuid);
				}
			}

			const auto now = std::chrono::steady_clock::now();
			if (members != notified)
			{
				notified = std::move(members);
				retry_until = now + 90s;
				next_notification = {};
			}

			if (now < retry_until && now >= next_notification)
			{
				next_notification = now + 1s;
				if (!send_party_follow(private_party, game_party))
				{
					retry_until = {};
				}
			}
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
				console::info("[HQ] Loading Headquarters...\n");
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
			alignas(16) game::PartySessionInfo descriptor{};
			descriptor.id = utils::hook::invoke<std::uint64_t>(0x66EAF0_g, request.session.session_id.data());
			utils::hook::invoke<void>(0x66EB40_g, request.session.host_address.data(), descriptor.address);
			utils::hook::invoke<void>(0x66EA70_g, request.session.key.data(), descriptor.key);
			join_headquarters(descriptor);
		}

		void join_social_party(const party::internal_connect_request& request)
		{
			if (request.custom_invite && party::has_session(request.session.session_id))
			{
				cancel_pending_connection();
				return;
			}

			// Native invite join (496560) reads party type from descriptor +64.
			// Private parties are type 1; game parties are 0. Session_Init (6FFD70)
			// copies this same 68-byte descriptor into the temporary session.
			alignas(16) std::array<std::uint8_t, 68> descriptor{};
			const auto id = utils::hook::invoke<std::uint64_t>(0x66EAF0_g, request.session.session_id.data());
			std::memcpy(descriptor.data(), &id, sizeof(id));
			utils::hook::invoke<void>(0x66EB40_g, request.session.host_address.data(), descriptor.data() + 8);
			utils::hook::invoke<void>(0x66EA70_g, request.session.key.data(), descriptor.data() + 45);
			descriptor[64] = 1;

			const auto token = request.friend_join ? 0 : request.discord_token;
			utils::hook::invoke<void>(0x1E4990_g, token);
			custom_invite_token = token != 0;
			utils::hook::invoke<void>(0x496560_g, game::CL_ControllerIndexFromClientNum(0),
				descriptor.data(), request.friend_join ? 0 : 1, 1);

			// The invite entry point starts a backend QoS probe (state 3), which cannot
			// resolve raw S2x peers. Our session query already verified this endpoint.
			// Enter the normal connect state with the same retry budget as a successful
			// probe (0x494BC0) or a Custom Match join (0x497B50).
			auto* join_info = utils::hook::invoke<game::PartyAtomicJoinInfo*>(0x47D270_g, 1);
			if (join_info->state == 3 && join_info->addressValid
				&& joined_social_session_id == request.session.session_id)
			{
				const auto* connect_attempts = game::Dvar_FindMalleableVar("pt_connectAttempts");
				if (connect_attempts)
				{
					join_info->connectAttempts = connect_attempts->current.integer;
					join_info->state = 1;
				}
			}

			utils::hook::invoke<void>(0xA74A50_g, descriptor.data() + 45);
			utils::hook::invoke<void>(0xA74AA0_g, descriptor.data());
		}

		void quit_stub()
		{
			party::cancel_pending_connection();

			if (!game::is_local_play())
			{
				auto* private_party = game::Party_GetPrivatePartyData();
				if (private_party && private_party->session
					&& utils::hook::invoke<bool>(0x471200_g, private_party)
					&& game::Party_AreWeHost(private_party))
				{
					// Stop the social party with native endparty reason 1 (disband).
					utils::hook::invoke<void>(0x4843C0_g, private_party, 1);
				}
			}

			quit_hook.invoke<void>();
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
		const int max_players, const std::uint64_t attempt_id, const bool friend_join, const std::uint32_t discord_token,
		const bool custom_invite, const bool follow_party)
	{
		const auto kind = party::session::classify(info.get("party_session"));
		if (kind == party::session::kind::none)
		{
			return false;
		}

		const auto is_hq_join = kind == party::session::kind::headquarters;
		const auto is_social_join = kind == party::session::kind::social;
		if (follow_party && (!is_party_follow_current(attempt_id) || is_social_join
			|| (party_follow->party_type == headquarters::party_type) != is_hq_join))
		{
			return true;
		}

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

		// A map the lobby named only stays in the arena catalog once the join proceeds
		arena::map_registration_scope registration;

		if (!party::session::valid_descriptor(session)
			|| max_players < 1
			|| max_players > party::session::get_player_limit(kind)
			|| (is_hq_join
				&& (max_players != headquarters::player_capacity
					|| map_name != headquarters::map_name || gametype != headquarters::gametype))
			|| (kind == party::session::kind::dedicated
				&& !parse_match_sequence(info.get("party_match_sequence"), match_sequence))
			|| (!is_social_join && (!validate_map_and_gametype(map_name, gametype)
				|| !party::validate_gametype(gametype)))
			|| (is_social_join && (!map_name.empty() || !gametype.empty())))
		{
			console::error("Connection failed: invalid hosted-party session data.\n");
			return true;
		}

		if (!is_hq_join && !is_social_join)
		{
			console::info("Joining %s lobby on map '%s' gametype '%s'.\n",
				kind == party::session::kind::custom ? "custom match" : "hosted dedicated",
				map_name.data(), gametype.data());
		}

		if (!party::is_connection_attempt_current(attempt_id))
		{
			return true;
		}

		registration.keep();
		auto pending_follow = follow_party ? std::move(party_follow) : std::nullopt;
		cancel_pending_connection();
		party_follow = std::move(pending_follow);

		auto target = from;
		target.localNetID = game::NS_SERVER;

		const auto generation = pending_join_generation;
		const auto deadline = std::chrono::steady_clock::now() + hq_preparation_timeout;
		const party::internal_connect_request request{
			attempt_id, session, map_name, gametype, friend_join, discord_token, custom_invite, follow_party
		};
		scheduler::schedule([request, target, max_players, match_sequence, kind,
			generation, deadline, preparing_hq = false]() mutable
		{
			if (!party::is_connection_attempt_current(request.attempt_id)
				|| generation != pending_join_generation
				|| (request.follow_party && !is_party_follow_current(request.attempt_id)))
			{
				return scheduler::cond_end;
			}

			if (kind == party::session::kind::headquarters)
			{
				if (!game::environment::is_multiplayer() || game::is_local_play()
					|| (!*game::virtualLobby_Loaded && !*game::virtualLobby_Requested))
				{
					console::error("[HQ] Return to the online Multiplayer menu before joining.\n");
					return scheduler::cond_end;
				}

				if (std::chrono::steady_clock::now() >= deadline)
				{
					console::error("[HQ] Loading timed out; join cancelled.\n");
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
				request.session.session_id, request.map_name, request.gametype, max_players, request.friend_join, request.discord_token, request.custom_invite
			};
			if (request.follow_party)
			{
				complete_party_follow(request);
			}
			else if (kind == party::session::kind::headquarters)
			{
				join_headquarters(request);
				cancel_pending_connection();
			}
			else if (kind == party::session::kind::social)
			{
				join_social_party(request);
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

	bool is_party_follow_current(const std::uint64_t attempt_id)
	{
		auto* private_party = game::Party_GetPrivatePartyData();
		return party_follow && party_follow->attempt_id == attempt_id
			&& party::is_connection_attempt_current(attempt_id)
			&& std::chrono::steady_clock::now() < party_follow->expires
			&& private_party && private_party->session && utils::hook::invoke<bool>(0x471200_g, private_party)
			&& private_party->session->sessionId == party_follow->private_session
			&& !game::Party_AreWeHost(private_party)
			&& utils::hook::invoke<bool>(0x479490_g, private_party, &party_follow->leader);
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

		auto* game_lobby = game::Lobby_GetPartyData(0);
		if (game::environment::is_multiplayer()
			&& has_joined_session(game_lobby, 0, joined_custom_session_id)
			&& utils::hook::invoke<bool>(0x471200_g, game_lobby)
			&& game::PartySettings_GetPrivateMatch(&game_lobby->settings) == 0
			&& game::PartySettings_GetRankedMatch(&game_lobby->settings) == 0)
		{
			// Map sessions use online stats without the private-match flag. Read the
			// received party gametype instead of the client's stale playlist label.
			const auto* gametype = game::Party_GetGameType(game_lobby);
			return gametype ? gametype : "";
		}

		return party::session::is_dedicated_lobby(game_lobby)
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
		if (custom_invite_token)
		{
			utils::hook::invoke<void>(0x1E49B0_g);
			custom_invite_token = false;
		}

		party_follow.reset();
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
		joined_social_session_id.clear();
		joined_game_target = {};
		joined_hq_target = {};
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

			quit_hook.create(game::Com_Quit_f, quit_stub);

			cl_connect_and_preload_map_hook.create(
				game::CL_ConnectAndPreloadMap, cl_connect_and_preload_map_stub);
			party_atomic_setup_potential_host_hook.create(
				0x497EF0_g, party_atomic_setup_potential_host_stub);

			party_atomic_request_join_hook.create(0x497B50_g, party_atomic_request_join_stub);

			// The online invite path removes client 1 before constructing the join.
			// For a validated hosted join with two active local players, reuse the
			// native controller-pair path and its local sign-in check. Only these
			// two predicates change; session setup keeps the real online mode.
			utils::hook::call(0x497B82_g, party_join_uses_local_controllers_stub);
			utils::hook::call(0x483E0D_g, party_join_uses_local_controllers_stub);

			utils::hook::call(0x497767_g, party_atomic_activate_lobby_stub);

			// CL_Connect and RequestJoin normally discard the social party. Keep it
			// only when its leader selects an S2x server, then use native hostPresence.
			utils::hook::call(0x6D1A5_g, stop_private_party_for_join);
			utils::hook::call(0x497CF4_g, stop_private_party_for_join);
			utils::hook::call(0x497D06_g, clear_private_session_for_join);
			utils::hook::call(0x4897DA_g, write_party_follow_session);
			utils::hook::call(0x4898DA_g, find_follow_reservation_stub);
			utils::hook::call(0x471643_g, party_atomic_is_joining_follow_session_stub);
			party_client_host_presence_hook.create(0x471400_g, party_client_host_presence_stub);
			scheduler::loop(notify_party_members, scheduler::main, 250ms);

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

REGISTER_COMPONENT(party_client::component)
