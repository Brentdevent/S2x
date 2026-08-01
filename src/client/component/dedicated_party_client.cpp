#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "dedicated_party_client.hpp"
#include "dedicated_party.hpp"
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
		constexpr auto max_party_members = 48;

		utils::hook::detour cl_connect_and_preload_map_hook;
		utils::hook::detour party_atomic_setup_potential_host_hook;
		utils::hook::detour party_client_handle_go_hook;
		utils::hook::detour party_client_process_party_state_hook;
		utils::hook::detour party_is_member_ui_visible_hook;

		struct hosted_party_join_state_t
		{
			bool active{};
			std::uint64_t attempt_id{};
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
			std::string map_name{};
			std::string gametype{};
			std::string sync_challenge{};
			game::PartyData* game_lobby{};
			int max_players{};
			bool sync_after_next_go{};
		};

		hosted_party_join_state_t hosted_party_join_state{};
		hosted_dedicated_party_state_t hosted_dedicated_party_state{};
		bool hosted_dedicated_go_in_progress{};

		bool is_hosted_dedicated_game_lobby(game::PartyData* party_data)
		{
			if (!party_data)
			{
				return false;
			}

			if (game::environment::is_dedicated())
			{
				return dedicated_party::is_active()
					&& (party_data == game::Lobby_GetPartyData(0)
						|| game::Party_AreWeHost(party_data));
			}

			return !hosted_dedicated_party_state.session_id.empty()
				&& (party_data == hosted_dedicated_party_state.game_lobby
					|| party_data == game::Lobby_GetPartyData(0));
		}

		int get_hosted_dedicated_party_max_players()
		{
			if (game::environment::is_dedicated())
			{
				if (!dedicated_party::is_active())
				{
					return -1;
				}

				const auto* dvar = game::Dvar_FindMalleableVar("party_maxplayers");
				return dvar ? dvar->current.integer : -1;
			}

			return hosted_dedicated_party_state.session_id.empty()
				? -1
				: hosted_dedicated_party_state.max_players;
		}

		bool is_dedicated_host_member(game::PartyData* party_data, const int member_index)
		{
			if (game::Party_IsHost(party_data, member_index))
			{
				return true;
			}

			if (game::environment::is_dedicated())
			{
				return game::Party_IsMemberLocalPlayer(party_data, member_index);
			}

			return false;
		}

		bool party_is_member_ui_visible_stub(game::PartyData* party_data, const int member_index)
		{
			if (is_hosted_dedicated_game_lobby(party_data)
				&& is_dedicated_host_member(party_data, member_index))
			{
				return false;
			}

			return party_is_member_ui_visible_hook.invoke<bool>(party_data, member_index);
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

			if (!is_hosted_dedicated_game_lobby(party_data))
			{
				return -1;
			}

			auto count = 0;
			for (auto member_index = 0; member_index < max_party_members; ++member_index)
			{
				if (party_is_member_ui_visible_stub(party_data, member_index))
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

		void apply_hosted_party_capacity(game::PartyData* party_data = nullptr)
		{
			const auto max_players = hosted_dedicated_party_state.max_players;
			if (max_players < 1
				|| max_players > game::environment::get_online_mode_info().max_players)
			{
				return;
			}

			auto apply = [max_players](game::PartyData* target)
			{
				if (target)
				{
					game::Party_SetMaxClients(target, max_players);
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

		bool is_session_hex_string(const std::string& value, const std::size_t expected_size)
		{
			return value.size() == expected_size
				&& std::all_of(value.begin(), value.end(), [](const unsigned char character)
				{
					return std::isxdigit(character) != 0;
				});
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

		void party_client_process_party_state_stub(game::PartyData* party_data,
			std::uint32_t* active_client, game::netadr_s* from)
		{
			party_client_process_party_state_hook.invoke<void>(
				party_data, active_client, from);

			if (!is_hosted_dedicated_party_address(from))
			{
				return;
			}

			hosted_dedicated_party_state.game_lobby = party_data;
			apply_hosted_party_capacity(party_data);
			refresh_presentation();

			// Public partystate applies its playlist rules after parsing and can replace
			// the dedicated host's free-form map/gametype with a local default. Keep the
			// native party state authoritative for UI and gameplay team initialization.
			if (!hosted_dedicated_party_state.map_name.empty()
				&& !hosted_dedicated_party_state.gametype.empty())
			{
				update_hosted_dedicated_party_match(
					hosted_dedicated_party_state.map_name,
					hosted_dedicated_party_state.gametype,
					game::environment::is_multiplayer());
			}

			if (game::virtual_lobby_loaded())
			{
				// Refresh once after a match so the next rotation selection is learned.
				request_hosted_dedicated_party_sync();
			}
		}

		std::int64_t party_client_handle_go_stub(game::PartyData* party_data, void* command_data,
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
			if (hosted_dedicated_go_in_progress
				&& game::environment::is_zombies())
			{
				// Stock Zombies ready-up otherwise consumes the go command without
				// entering its native preload path. A dedicated go is the server's
				// readiness decision, so confirm it through the stock setter.
				const auto controller_index =
					game::CL_ControllerIndexFromClientNum(0);
				game::PartyClient_SetLocalReadyUpFlag(controller_index);
			}

			const auto result = party_client_handle_go_hook.invoke<std::int64_t>(
				party_data, command_data, from, message);
			hosted_dedicated_go_in_progress = false;

			if (!map_name_value.empty())
			{
				update_hosted_dedicated_party_match(
					map_name_value, gametype_value,
					game::environment::is_multiplayer());
			}

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

		bool party_atomic_setup_potential_host_stub(const int controller_index,
			const void* session_info, const int party_type, const int max_players,
			const int a5, const int a6, game::PartyAtomicJoinInfo* join_info)
		{
			const auto is_hosted_party_join = pending_hosted_party_join_matches(session_info);
			const auto pending_join = hosted_party_join_state;
			const auto setup_max_players = is_hosted_party_join
				? pending_join.max_players
				: max_players;

			const auto result = party_atomic_setup_potential_host_hook.invoke<bool>(
				controller_index, session_info, party_type, setup_max_players, a5, a6, join_info);

			if (!is_hosted_party_join)
			{
				return result;
			}

			if (!party::is_connection_attempt_current(pending_join.attempt_id))
			{
				return result;
			}

			const auto target = pending_join.target;
			const auto session_id = pending_join.session_id;
			const auto map_name = pending_join.map_name;
			const auto gametype = pending_join.gametype;
			const auto hosted_max_players = setup_max_players;
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
					session_info, 0, hosted_max_players, a5))
				{
					console::error("Hosted dedicated lobby: native party session setup failed.\n");
					return false;
				}
			}

			join_info->address = target;
			join_info->addressValid = 1;
			hosted_dedicated_party_state = {};
			hosted_dedicated_party_state.target = target;
			hosted_dedicated_party_state.session_id = session_id;
			hosted_dedicated_party_state.map_name = map_name;
			hosted_dedicated_party_state.gametype = gametype;
			hosted_dedicated_party_state.game_lobby = game::Lobby_GetPartyData(0);
			hosted_dedicated_party_state.max_players = hosted_max_players;
			apply_hosted_party_capacity(hosted_dedicated_party_state.game_lobby);

			console::info("Hosted dedicated lobby: joining through %s.\n",
				network::net_adr_to_string(target));
			return true;
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
				return get_gametype();
			};

			lobby["GetDedicatedPartyMemberCount"] = []
			{
				return get_hosted_dedicated_party_member_count();
			};

			lobby["GetDedicatedPartyMaxPlayers"] = []
			{
				return get_hosted_dedicated_party_max_players();
			};

			refresh_presentation();
		}
	}

	bool try_handle_join(const game::netadr_s& from, const utils::info_string& info,
		const int max_players, const std::uint64_t attempt_id)
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
			|| max_players < 1
			|| max_players > game::environment::get_online_mode_info().max_players
			|| !validate_map_and_gametype(map_name, gametype)
			|| !party::validate_gametype(gametype))
		{
			console::error("Connection failed: invalid hosted-party session data.\n");
			return true;
		}

		console::info("Joining hosted dedicated lobby on map '%s' gametype '%s'.\n",
			map_name.data(), gametype.data());

		if (!party::is_connection_attempt_current(attempt_id))
		{
			return true;
		}

		cancel_pending_connection();

		auto target = from;
		target.localNetID = game::NS_SERVER;

		scheduler::once([target, host_address, key, session_id, map_name, gametype, max_players,
			attempt_id]()
		{
			if (!party::is_connection_attempt_current(attempt_id))
			{
				return;
			}

			// This is the seven-argument command emitted by S2's stock JoinServer menu.
			// CL_Connect parses the session descriptor and calls PartyAtomic_RequestJoin.
			hosted_party_join_state = {
				true, attempt_id, target, session_id, map_name, gametype, max_players
			};
			party::execute_internal_connect({
				attempt_id, host_address, key, session_id, map_name, gametype
			});
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

		hosted_dedicated_party_state.sync_challenge.clear();
		if (info.get("party_session") == "1"
			&& info.get("session_id") == hosted_dedicated_party_state.session_id)
		{
			hosted_dedicated_party_state.max_players = max_players;
			apply_hosted_party_capacity(hosted_dedicated_party_state.game_lobby);
			refresh_presentation();
			update_hosted_dedicated_party_match(
				info.get("party_mapname"), info.get("party_gametype"),
				game::environment::is_multiplayer());
		}

		return true;
	}

	std::string get_gametype()
	{
		if (game::environment::is_dedicated())
		{
			return dedicated_party::get_current_gametype();
		}

		return hosted_dedicated_party_state.gametype;
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
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			ui_scripting::on_start(install_lobby_functions);

			// The frontend lobby row model uses this predicate. PartyData still retains
			// its native host member; the character scene is filtered separately in LUI.
			party_is_member_ui_visible_hook.create(
				game::Party_IsMemberUIVisible, party_is_member_ui_visible_stub);

			if (game::environment::is_dedicated())
			{
				return;
			}

			cl_connect_and_preload_map_hook.create(
				game::CL_ConnectAndPreloadMap, cl_connect_and_preload_map_stub);
			party_atomic_setup_potential_host_hook.create(
				0x497EF0_g, party_atomic_setup_potential_host_stub);
			party_client_handle_go_hook.create(
				game::PartyClient_HandleGo, party_client_handle_go_stub);
			party_client_process_party_state_hook.create(
				game::PartyClient_ProcessPartyState, party_client_process_party_state_stub);
		}
	};
}

REGISTER_COMPONENT(dedicated_party_client::component)
