#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "party_session.hpp"
#include "dedicated_party.hpp"
#include "dedicated_party_client.hpp"
#include "headquarters.hpp"

#include <utils/hook.hpp>

namespace party::session
{
	namespace
	{
		utils::hook::detour party_is_member_ui_visible_hook;
		utils::hook::detour session_modify_hook;

		bool is_hex(const std::string_view value, const std::size_t size)
		{
			return value.size() == size && std::all_of(value.begin(), value.end(), [](const char c)
			{
				return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
			});
		}

		int session_get_gameplay_member_xuids_stub(game::SessionData* session, std::uint64_t* xuids)
		{
			const auto count = utils::hook::invoke<int>(0x7B1E50_g, session, xuids);
			auto* party_data = utils::hook::invoke<game::PartyData*>(0x6FDE30_g, session);
			if (count <= 0 || !is_dedicated_lobby(party_data))
			{
				return count;
			}

			// CG's player-configstring reconciliation removes session members that
			// have no gameplay client. Dedicated game and HQ owners must survive
			// this scan so the native party keeps its host. Filter only this gameplay
			// roster, not the session.
			const auto host_xuid = utils::hook::invoke<std::uint64_t>(
				0x6FDE70_g, session, party_data->hostIndex);
			const auto* end = std::remove(xuids, xuids + count, host_xuid);
			return static_cast<int>(end - xuids);
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

		int party_is_member_ui_visible_stub(game::PartyData* party_data, const int member_index)
		{
			if (is_dedicated_lobby(party_data)
				&& is_dedicated_host_member(party_data, member_index))
			{
				return 0;
			}

			return party_is_member_ui_visible_hook.invoke<int>(party_data, member_index);
		}

		void session_modify_stub(const int controller_index, game::SessionData* session,
			const int flags, int public_slots, const int private_slots, const int spectator_slots)
		{
			auto* party_data = utils::hook::invoke<game::PartyData*>(0x6FDE30_g, session);
			if (headquarters::is_host() && party_data == game::Lobby_GetPartyData(headquarters::party_type))
			{
				public_slots = std::max(0, headquarters::session_capacity - private_slots);
			}
			else if (party_data == game::Lobby_GetPartyData(0)
				&& (game::environment::is_dedicated()
					? is_dedicated_lobby(party_data)
					: dedicated_party_client::has_dedicated_session(party_data)))
			{
				// Native lobby updates resize sessions again after map transitions.
				// Retain the owner's fixed index even when the human limit is smaller.
				public_slots = dedicated_party::get_session_capacity() - private_slots;
			}

			session_modify_hook.invoke<void>(controller_index, session, flags,
				public_slots, private_slots, spectator_slots);
		}

		void party_set_gameplay_max_clients_stub(game::dvar_t* dvar, const int value)
		{
			const auto max_players = get_dedicated_max_players();
			// Party settings and partystate include the owner. Both the host and
			// receiving clients must allocate gameplay state for human players only.
			game::Dvar_SetInt(dvar, max_players > 0 ? max_players : value);
		}
	}

	bool is_dedicated_lobby(game::PartyData* party_data)
	{
		if (!party_data)
		{
			return false;
		}

		if (game::environment::is_dedicated())
		{
			if (headquarters::is_host())
			{
				return party_data == game::Lobby_GetPartyData(headquarters::party_type);
			}

			return dedicated_party::is_active()
				&& (party_data == game::Lobby_GetPartyData(0)
					|| game::Party_AreWeHost(party_data));
		}

		return (dedicated_party_client::has_dedicated_session(party_data)
			|| dedicated_party_client::has_headquarters_session(party_data))
			&& utils::hook::invoke<bool>(0x471200_g, party_data); // Lobby_IsInLobby
	}

	int get_dedicated_max_players()
	{
		if (game::environment::is_dedicated())
		{
			return dedicated_party::get_max_players();
		}

		return is_dedicated_lobby(game::Lobby_GetPartyData(0))
			? dedicated_party_client::get_joined_max_players()
			: -1;
	}

	// party_session: absent/0 = direct game, 1 = dedicated, 2 = custom, 3 = HQ.
	kind classify(const std::string_view value)
	{
		if (value.empty() || value == "0")
		{
			return kind::none;
		}
		if (value == "1")
		{
			return kind::dedicated;
		}
		if (value == "2")
		{
			return kind::custom;
		}
		if (value == "3")
		{
			return kind::headquarters;
		}
		return kind::invalid;
	}

	int get_player_limit(const kind session_kind)
	{
		if (session_kind == kind::invalid)
		{
			return 0;
		}
		if (session_kind == kind::headquarters)
		{
			return game::environment::is_multiplayer() ? headquarters::player_capacity : 0;
		}

		return game::environment::get_online_mode_info().max_players;
	}

	bool valid_descriptor(const descriptor& session)
	{
		return is_hex(session.host_address, 80) && is_hex(session.key, 32) && is_hex(session.session_id, 16);
	}

	descriptor describe(const game::SessionData& session)
	{
		std::array<char, 81> address{};
		std::array<char, 33> key{};
		std::array<char, 17> id{};
		game::Session_HostAddressToString(&session.hostAddress, address.data());
		game::Session_KeyToString(&session.sessionKey, key.data());
		game::Session_IdToString(session.sessionId, id.data());
		return { address.data(), key.data(), id.data() };
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			// The frontend lobby row model uses this predicate. PartyData still retains
			// its native host member; the character scene is filtered separately in LUI.
			party_is_member_ui_visible_hook.create(
				game::Party_IsMemberUIVisible, party_is_member_ui_visible_stub);

			// Party-to-game handoff and partystate receipt must exclude the owner
			// when copying party capacity into the gameplay limit.
			utils::hook::call(0x475139_g, party_set_gameplay_max_clients_stub);
			utils::hook::call(0x47772C_g, party_set_gameplay_max_clients_stub);
			session_modify_hook.create(0x6FE6A0_g, session_modify_stub);

			if (game::environment::is_dedicated())
			{
				// Only headless servers pin memory capacity to the human limit.
				// Clients need the native 48-slot virtual-lobby allocation after a
				// match. Clamping it leaves too little memory for lobby SV_Startup
				// and triggers Memory Error: 6 161. Keep native capacity restoration
				// for an existing client allocation intact as well.
				utils::hook::call(0x62538_g, party_set_gameplay_max_clients_stub);
				utils::hook::call(0x625FE_g, party_set_gameplay_max_clients_stub);
				return;
			}

			// Preserve the dedicated owner when CG reconciles gameplay configstrings.
			utils::hook::call(0x436608_g, session_get_gameplay_member_xuids_stub);
		}
	};
}

REGISTER_COMPONENT(party::session::component)
