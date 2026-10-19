#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "headquarters.hpp"
#include "party.hpp"
#include "scheduler.hpp"

#include "console/console.hpp"

#include <utils/flags.hpp>
#include <utils/hook.hpp>

namespace headquarters
{
	namespace
	{
		static_assert(session_capacity == std::extent_v<decltype(game::PartyData::members)>);
		static_assert(session_capacity == std::extent_v<decltype(game::SessionData::members)>);

		constexpr auto startup_timeout = 120s;

		utils::hook::detour set_max_clients_hook;
		utils::hook::detour start_party_hook;
		bool started{};

		bool is_hq_party(game::PartyData* party_data)
		{
			// Session initialization (MP RVA 0x6FDEB0) maps hubSession to
			// Session_GetData(1) and Lobby_GetPartyData(2), not game party 0.
			return party_data && party_data == game::Lobby_GetPartyData(party_type);
		}

		void set_max_clients_stub(game::PartyData* party_data, const int value)
		{
			set_max_clients_hook.invoke<void>(party_data, is_hq_party(party_data) ? session_capacity : value);
		}

		char start_party_stub(game::PartyData* party_data, const unsigned int local_client,
			const unsigned int controller, const char flags, const int private_slots, int public_slots)
		{
			if (is_hq_party(party_data))
			{
				game::Party_SetMaxClients(party_data, session_capacity);
				public_slots = std::max(0, session_capacity - private_slots);
			}

			return start_party_hook.invoke<char>(party_data, local_client, controller,
				flags, private_slots, public_slots);
		}
	}

	bool is_host()
	{
		return game::environment::is_multiplayer() && game::environment::is_dedicated()
			&& utils::flags::has_flag("-hq");
	}

	void start()
	{
		if (!is_host() || started)
		{
			return;
		}

		started = true;
		console::info("[HQ] Starting...\n");

		// Stock handle_fullhub (persistentforeground.lua) uses this command.
		// SV_VirtualLobby_f -> 0x484130 switches out of the slim scene, stops
		// the old HQ, and requests the native full-HQ lifecycle. No game lobby
		// or map rotation is created. Keep its local owner in the native slot.
		game::Cbuf_AddText(0, "virtuallobby fullhub\n");

		const auto deadline = std::chrono::steady_clock::now() + startup_timeout;
		scheduler::schedule([deadline]
		{
			party::session::connect_info info{};
			if (headquarters::get_connect_info(info))
			{
				console::info("[HQ] Ready for players (%d slots).\n", player_capacity);
				return scheduler::cond_end;
			}

			if (std::chrono::steady_clock::now() >= deadline)
			{
				console::error("[HQ] Startup timed out.\n");
				return scheduler::cond_end;
			}

			return scheduler::cond_continue;
		}, scheduler::pipeline::main, 100ms);
	}

	bool get_connect_info(party::session::connect_info& info)
	{
		if (!is_host() || !started || !game::SV_Loaded()
			|| party::loaded_map_name() != map_name || party::loaded_gametype() != gametype)
		{
			return false;
		}

		auto* party_data = game::Lobby_GetPartyData(party_type);
		auto* session = game::Session_GetData(1);
		if (!party_data || !session || !session->sessionId || !game::Party_AreWeHost(party_data)
			|| !utils::hook::invoke<bool>(0x471200_g, party_data))
		{
			return false;
		}

		info = {};
		info.session = party::session::describe(*session);
		info.map_name = map_name;
		info.gametype = gametype;
		info.max_players = player_capacity;
		info.match_running = true;

		for (auto member_index = 0; member_index < static_cast<int>(std::size(party_data->members)); ++member_index)
		{
			if (party_data->members[member_index].state >= 5
				&& !game::Party_IsMemberLocalPlayer(party_data, member_index)
				&& !game::Party_IsHost(party_data, member_index))
			{
				++info.player_count;
			}
		}

		return party::session::valid_descriptor(info.session);
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (!is_host())
			{
				return;
			}

			set_max_clients_hook.create(game::Party_SetMaxClients, set_max_clients_stub);
			start_party_hook.create(0x491DE0_g, start_party_stub);
		}
	};
}

REGISTER_COMPONENT(headquarters::component)
