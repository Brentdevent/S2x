#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "pause_hard.hpp"
#include "command.hpp"
#include "console/console.hpp"
#include "network.hpp"
#include "party.hpp"
#include "scheduler.hpp"

#include "game/game.hpp"
#include "game/pause_hard.hpp"

#include <utils/cryptography.hpp>
#include <utils/hook.hpp>
#include <utils/string.hpp>

#include <bit>
#include <charconv>
#include <deque>
#include <powrprof.h>

#pragma comment(lib, "Powrprof.lib")

namespace pause_hard
{
	namespace
	{
		using protocol::message;
		using protocol::phase;

		constexpr auto client_zombie = 1;
		constexpr auto client_active = 5;

		// Prefix only, not the size of clientConnection_t. Verified in
		// CL_ConnectAndPreloadMap / CL_ParseGamestate and CL_Frame (0x7547B).
		struct connection_prefix
		{
			char pad[8];
			int last_packet_time;
			char pad_c[4];
			game::netadr_s server;
		};
		static_assert(offsetof(connection_prefix, last_packet_time) == 0x8);
		static_assert(offsetof(connection_prefix, server) == 0x10);
		static_assert(sizeof(connection_prefix) == 0x24);

		struct identity
		{
			std::uint64_t xuid{};
			std::array<char, 17> guid{};
			game::netadr_s address{};
			int qport{};
			int connect_time{};
			bool local{};
		};

		enum class event_type
		{
			bootstrap,
			state,
			ack,
			recover,
			leave,
			remove,
			start,
			ready,
			unready,
			status,
		};

		struct event
		{
			event_type type{};
			game::netadr_s from{};
			message packet{};
			std::uint64_t epoch{};
			std::uint64_t generation{};
			std::uint64_t round{};
			std::size_t slot{};
		};

		struct removal
		{
			identity peer;
			game::mp::client_t* clients{};
			std::uint64_t epoch{};
			std::uint64_t match{};
			bool completed{};
			bool success{};
		};

		struct removed_peer
		{
			game::netadr_s address;
			message notice;
		};

		// Removal requests and results cross the main/server thread boundary.
		std::mutex removal_mutex;
		std::array<std::optional<removal>, protocol::capacity> removals;

		std::atomic_bool accepting{};
		std::atomic_bool server_frozen{};
		std::atomic_bool client_frozen{};
		std::atomic_bool timeout_exempt{};

		std::atomic_uint64_t epoch{1};
		std::atomic_uint64_t visible_generation{};
		std::atomic_uint64_t visible_round{};
		std::atomic_uint64_t power_generation{};

		HPOWERNOTIFY power_notification{};

		std::mutex inbox_mutex;
		std::mutex display_mutex;

		std::deque<event> inbox;
		std::string display;

		// Protocol and engine state below is owned by the main pipeline. Packet
		// callbacks only copy bounded messages; renderer/hooks read published state.
		protocol::host host;
		protocol::client client;

		std::array<identity, protocol::capacity> roster;
		game::mp::client_t* host_clients{};
		game::PartyData* held_party{};

		std::uint64_t held_session{};
		std::uint64_t seen_power_generation{};
		std::uint64_t last_tick_at{};
		game::netadr_s client_host{};
		std::uint64_t client_host_xuid{};
		std::uint64_t next_probe_at{};
		std::uint64_t terminal_at{};

		bool owns_local_pause{};
		int saved_client_pause{};

		std::uint8_t pending_removals{};
		// Retain unacknowledged dismissals through release and subsequent pauses
		// in this match, so an excluded sleeping client can leave on its return.
		std::vector<removed_peer> removed_peers;
		std::uint64_t next_removal_at{};

		utils::hook::detour shutdown_hook;
		utils::hook::detour restart_hook;
		utils::hook::detour spawn_hook;
		utils::hook::detour party_frame_hook;

		std::uint64_t now_ms()
		{
			// GetTickCount64 includes sleep; only protocol deadlines use this clock.
			return GetTickCount64();
		}

		ULONG CALLBACK power_callback(void*, const ULONG type, void*)
		{
			// Power callbacks may run on another thread. Do not call the engine,
			// wait for peers, or put the PC to sleep here.
			if (type == PBT_APMSUSPEND || type == PBT_APMRESUMEAUTOMATIC)
			{
				++power_generation;
			}

			return ERROR_SUCCESS;
		}

		std::uint64_t random_id()
		{
			std::uint64_t value{};
			utils::cryptography::random::get_data(&value, sizeof(value));
			return value | 1;
		}

		std::uint32_t match_id()
		{
			// CL_ParseGamestate (0x462CEB) and its restart path (0x462EA8)
			// obtain the server ID from configstring 3, then update this global.
			return *reinterpret_cast<const std::uint32_t*>(0x248934C_g);
		}

		connection_prefix* connection()
		{
			return *reinterpret_cast<connection_prefix**>(0x1BD3D00_g);
		}

		bool remote_address(const game::netadr_s& address)
		{
			// Sys_GetPacket writes type 3 for normal UDP (0x7B0EAE), whereas
			// NET_StringToAdr/SOCKS can produce type 4. The shared enum names
			// type 3 NA_BROADCAST, but it is also used by connected native peers.
			return (address.type == game::NA_BROADCAST || address.type == game::NA_IP) && address.addr &&
				address.addr != 0xFFFFFFFFu && address.port;
		}

		bool same_endpoint(const game::netadr_s& a, const game::netadr_s& b)
		{
			return remote_address(a) && remote_address(b) && a.addr == b.addr && a.port == b.port;
		}

		game::dvar_t* client_pause_dvar()
		{
			return *reinterpret_cast<game::dvar_t**>(0x1BD37B0_g);
		}

		bool playing_zombies()
		{
			return accepting && game::environment::is_zombies() && !game::environment::is_dedicated() &&
				!game::is_local_play() && !game::virtual_lobby_loaded() && game::CL_IsLocalClientInGame(0);
		}

		game::PartyData* private_party()
		{
			if (!playing_zombies())
			{
				return nullptr;
			}

			// Party_IsRunning becomes false during gameplay. Live_GetGameParty can
			// then fall back to the social party; bind the local client's game lobby
			// and its in-party flag instead (0x47D050 / 0x471200).
			auto* party_data = game::Lobby_GetPartyData(0);
			if (!party_data || !party_data->session || !party_data->session->sessionId ||
				!utils::hook::invoke<bool>(0x471200_g, party_data) ||
				// PublicMatch is host-local: joined private clients retain its default 1.
				// PrivateMatch is replicated and required on both sides.
				(game::Party_AreWeHost(party_data) && game::Party_GetPublicMatch(party_data)) ||
				game::PartySettings_GetPrivateMatch(&party_data->settings) != 1 ||
				game::PartySettings_GetRankedMatch(&party_data->settings) ||
				std::string_view{game::Party_GetGameType(party_data)} != "zombies")
			{
				return nullptr;
			}

			return party_data;
		}

		void set_display(std::string text)
		{
			std::lock_guard lock{display_mutex};
			display = std::move(text);
		}

		void local_pause(const bool enabled)
		{
			if (enabled && !owns_local_pause)
			{
				saved_client_pause = client_pause_dvar()->current.integer;
				owns_local_pause = true;
				// The native Zombies pause entry sets cl->pauseResume (+0x16598).
				// CL_SetCGameTime then uses its normal first-snapshot time rebasing
				// on resume (0x86EC3), instead of adding the paused wall-clock time.
				utils::hook::invoke<void>(0x75010_g);
			}

			if (enabled)
			{
				if (client_pause_dvar()->current.integer != 1)
				{
					// CL_SetPaused replaces the catchers through 0x45FB70, preserving
					// only the console bit. Keep an existing LUI/chat capture so
					// refreshing the native pause cannot disable an open menu.
					const auto catchers = *game::keyCatchers;
					utils::hook::invoke<void>(0x745B10_g, 0u, 1); // CL_SetPaused
					*game::keyCatchers |= catchers;
				}
			}
			else if (owns_local_pause)
			{
				utils::hook::invoke<void>(0x745B10_g, 0u, saved_client_pause);
				owns_local_pause = false;
				// Snapshots stop with the simulation. Give the existing connection
				// a fresh native timeout interval to receive the first resumed packet.
				if (auto* conn = connection())
				{
					conn->last_packet_time = *reinterpret_cast<const int*>(0x1C7E1F0_g);
				}
			}

			client_frozen = enabled;
		}

		void clear_state(const bool keep_removed = false)
		{
			++epoch;
			{
				std::lock_guard lock{removal_mutex};
				removals = {};
			}

			pending_removals = 0;
			if (!keep_removed)
			{
				removed_peers.clear();
				next_removal_at = 0;
			}

			visible_generation = visible_round = 0;
			held_party = nullptr;
			held_session = last_tick_at = 0;
			seen_power_generation = power_generation;
			const bool was_host_paused = server_frozen.exchange(false);
			timeout_exempt = false;
			local_pause(false);

			if (was_host_paused)
			{
				auto* dvar = *reinterpret_cast<game::dvar_t**>(0x1BD37B8_g);
				if (dvar)
				{
					game::Dvar_SetInt(dvar, 0);
				}
			}

			host = {};
			client = {};
			roster = {};
			host_clients = nullptr;
			client_host = {};
			client_host_xuid = next_probe_at = terminal_at = 0;

			{
				std::lock_guard lock{inbox_mutex};
				inbox.clear();
			}

			set_display({});
		}

		void enqueue(event value)
		{
			if (!accepting)
			{
				return;
			}

			value.epoch = epoch;
			std::lock_guard lock{inbox_mutex};
			if (inbox.size() < 64)
			{
				inbox.push_back(std::move(value));
			}
		}

		void enqueue_command(const event_type kind, const std::size_t slot = 0)
		{
			event value{kind};
			value.generation = visible_generation;
			value.round = visible_round;
			value.slot = slot;
			enqueue(value);
		}

		void enqueue_packet(const event_type kind, const game::netadr_s& from, const std::string_view& data)
		{
			if (const auto packet = protocol::decode(data))
			{
				enqueue({kind, from, *packet});
			}
		}

		bool connection_matches(const game::mp::client_t& slot, const identity& peer)
		{
			return std::to_array(slot.guid) == peer.guid && slot.remoteAddress == peer.address &&
				slot.qport == peer.qport && slot.lastConnectTime == peer.connect_time && !slot.testClient;
		}

		bool read_roster(game::PartyData* party_data, std::array<identity, protocol::capacity>& result,
			std::uint8_t& mask, std::uint8_t& local_mask)
		{
			auto* clients = *game::mp::svs_clients;
			const auto count = *game::sv_maxclients;
			if (!clients || count < 2 || count > static_cast<int>(protocol::capacity))
			{
				return false;
			}

			mask = local_mask = 0;
			for (int i = 0; i < count; ++i)
			{
				const auto& slot = clients[i];
				if (!slot.state)
				{
					continue;
				}

				// SV's active gameplay state is 5 (0x6DE465, 0x6E0732).
				if (slot.state != client_active || slot.testClient || !slot.gentity || !party_data->members[i].xuid ||
					!party_data->session->members[i].active ||
					party_data->session->members[i].xuid != party_data->members[i].xuid)
				{
					return false;
				}

				const bool local = game::Party_IsMemberLocalPlayer(party_data, i) != 0;
				if (local ? slot.remoteAddress.type != game::NA_LOOPBACK : !remote_address(slot.remoteAddress))
				{
					return false;
				}

				result[i] = {party_data->members[i].xuid, std::to_array(slot.guid), slot.remoteAddress, slot.qport,
					slot.lastConnectTime, local};
				mask |= 1u << i;
				if (local)
				{
					local_mask |= 1u << i;
				}
			}

			// Split screen and bots are not supported.
			return std::popcount(mask) >= 2 && std::popcount(local_mask) == 1;
		}

		bool bound_host(game::PartyData* party_data)
		{
			return party_data && party_data == held_party && game::Party_AreWeHost(party_data) &&
				party_data->session->sessionId == host.session && match_id() == host.match &&
				host_clients && *game::mp::svs_clients == host_clients && party::server_running() && game::SV_Loaded() &&
				*game::sv_maxclients >= 2 && *game::sv_maxclients <= static_cast<int>(protocol::capacity);
		}

		bool removal_matches(game::PartyData* party_data, const std::size_t slot)
		{
			if (!bound_host(party_data) || slot >= static_cast<std::size_t>(*game::sv_maxclients) ||
				!host.can_remove(slot) || !connection_matches(host_clients[slot], roster[slot]))
			{
				return false;
			}

			const auto xuid = party_data->members[slot].xuid;
			const auto& member = party_data->session->members[slot];
			return (!xuid || xuid == roster[slot].xuid) && (!member.active || member.xuid == roster[slot].xuid) &&
				(host_clients[slot].state == client_active || host_clients[slot].state == client_zombie);
		}

		std::uint8_t connected_guests(game::PartyData* party_data)
		{
			std::uint8_t mask{};
			if (bound_host(party_data))
			{
				for (int i = 0; i < *game::sv_maxclients; ++i)
				{
					if (host.is_guest(i) && host_clients[i].state > client_zombie &&
						connection_matches(host_clients[i], roster[i]))
					{
						mask |= 1u << i;
					}
				}
			}

			return mask;
		}

		bool roster_matches(game::PartyData* party_data)
		{
			if (!bound_host(party_data))
			{
				return false;
			}

			for (int i = 0; i < *game::sv_maxclients; ++i)
			{
				const auto& slot = host_clients[i];
				if (!(host.roster & (1u << i)))
				{
					// Native dropped clients remain zombies (state 1) until normal
					// maintenance reclaims them after release. New occupants are not voters.
					if (slot.state > client_zombie)
					{
						return false;
					}

					continue;
				}

				if (pending_removals & (1u << i))
				{
					if (!removal_matches(party_data, i))
					{
						return false;
					}

					continue;
				}

				const auto& peer = roster[i];
				const bool local = game::Party_IsMemberLocalPlayer(party_data, i) != 0;
				if (slot.state != client_active || !slot.gentity || !connection_matches(slot, peer) ||
					party_data->members[i].xuid != peer.xuid || local != peer.local ||
					!party_data->session->members[i].active || party_data->session->members[i].xuid != peer.xuid)
				{
					return false;
				}
			}

			return true;
		}

		void request_removal(game::PartyData* party_data, const std::size_t slot)
		{
			if (!removal_matches(party_data, slot) || (pending_removals & (1u << slot)))
			{
				return;
			}

			// Reset votes before dispatching engine work. No confirmation may
			// release the hold while the native disconnect is still pending.
			host.recover();
			pending_removals |= 1u << slot;
			next_probe_at = 0;
			std::lock_guard lock{removal_mutex};
			removals[slot] = removal{roster[slot], host_clients, epoch.load(), host.match};
			console::info("Hard pause: removing player %zu. Everyone remaining must ready up again.\n", slot);
		}

		int server_network_done_stub()
		{
			{
				// This call is still inside SV's network ownership boundary. Run on
				// the server thread before signalling completion to SV_WaitServer.
				std::lock_guard lock{removal_mutex};
				for (std::size_t i = 0; i < removals.size(); ++i)
				{
					auto& work = removals[i];
					if (!work || work->completed)
					{
						continue;
					}

					work->completed = true;
					if (!accepting || !server_frozen || work->epoch != epoch || work->match != match_id() ||
						!game::SV_Loaded() || *game::mp::svs_clients != work->clients ||
						i >= static_cast<std::size_t>(*game::sv_maxclients))
					{
						continue;
					}

					auto& slot = work->clients[i];
					if (!connection_matches(slot, work->peer) || (slot.state != client_active && slot.state != client_zombie))
					{
						continue;
					}

					utils::hook::invoke<void>(0x6DAED0_g); // Enter server game work, as in SV packet processing.

					if (slot.state == client_active)
					{
						game::mp::SV_DropClient(&slot, "EXE_PLAYERKICKED", 1);
					}

					utils::hook::invoke<void>(0x6DD620_g, &slot); // Clear the native pending-drop reason.
					slot.lastPacketTime = *game::mp::svs_time;

					utils::hook::invoke<void>(0x6DAD80_g);
					work->success = slot.state == client_zombie && connection_matches(slot, work->peer);
				}
			}

			return utils::hook::invoke<int>(0x675480_g);
		}

		void finish_removals(game::PartyData* party_data)
		{
			std::array<std::optional<removal>, protocol::capacity> completed;
			{
				std::lock_guard lock{removal_mutex};
				for (std::size_t i = 0; i < removals.size(); ++i)
				{
					if (removals[i] && removals[i]->completed)
					{
						completed[i] = std::exchange(removals[i], {});
					}
				}
			}

			for (std::size_t i = 0; i < completed.size(); ++i)
			{
				if (!completed[i])
				{
					continue;
				}

				if (!completed[i]->success || completed[i]->epoch != epoch || !removal_matches(party_data, i))
				{
					host.check_presence(now_ms(), false);
					console::error("Hard pause: could not remove the player. The game will stay paused.\n");
					continue;
				}

				auto notice = host.packet(i);
				notice.state = phase::removed;
				notice.probe = notice.value = notice.wake = 0;

				// Remove the exact game-lobby member on main, using native party
				// cleanup. Live_GetGameParty may point at the social party in-game.
				utils::hook::invoke<std::int64_t>(0x48E3D0_g, party_data, static_cast<char>(i), 1u, "EXE_PLAYERKICKED");

				if (host.remove(i))
				{
					removed_peers.push_back({roster[i].address, notice});
					pending_removals &= static_cast<std::uint8_t>(~(1u << i));
					next_probe_at = next_removal_at = 0;
					console::info("Hard pause: removed player %zu. Players remaining: %d.\n", i,
						std::popcount(static_cast<unsigned>(host.roster)));
				}
			}
		}

		bool acknowledge_removal(const event& value)
		{
			const auto& msg = value.packet;
			if (value.type != event_type::ack || msg.state != phase::removed || msg.probe || msg.value || msg.wake)
			{
				return false;
			}

			std::erase_if(removed_peers, [&](const removed_peer& peer)
			{
				const auto& notice = peer.notice;
				return same_endpoint(value.from, peer.address) && msg.session == notice.session &&
					msg.generation == notice.generation && msg.match == notice.match && msg.token == notice.token &&
					msg.round == notice.round;
			});

			return true;
		}

		void start_pause(game::PartyData* party_data, const std::uint64_t now)
		{
			if (host.state != phase::idle || client.frozen())
			{
				return;
			}

			if (!party_data || !game::Party_AreWeHost(party_data) || !party::server_running() || !game::SV_Loaded())
			{
				console::info("Hard pause: only the host can pause a private co-op Zombies match.\n");
				return;
			}

			std::uint8_t mask{};
			std::uint8_t local_mask{};
			if (!read_roster(party_data, roster, mask, local_mask))
			{
				console::info("Hard pause: you need 2-4 human players, one per PC. Wait until everyone has joined.\n");
				return;
			}

			std::array<std::uint64_t, protocol::capacity> tokens{};
			for (auto& token : tokens)
			{
				token = random_id();
			}

			if (!host.start(party_data->session->sessionId, random_id(), mask, local_mask, tokens, now))
			{
				return;
			}

			host.match = match_id();
			host_clients = *game::mp::svs_clients;
			visible_generation = host.generation;
			visible_round = host.round;
			held_party = party_data;
			held_session = host.session;
			for (std::size_t i = 0; i < protocol::capacity; ++i)
			{
				if (!host.is_guest(i))
				{
					continue;
				}

				if (host_clients[i].reliableSequence - host_clients[i].reliableAcknowledge >= 64)
				{
					console::info("Hard pause: could not send the pause request. Try again.\n");
					clear_state(true);
					return;
				}
			}

			for (std::size_t i = 0; i < protocol::capacity; ++i)
			{
				if (!host.is_guest(i))
				{
					continue;
				}

				const auto payload = protocol::encode(host.packet(i));
				game::SV_SendServerCommand(&host_clients[i], game::SV_CMD_RELIABLE, "%s %s",
					protocol::bootstrap_command.data(), payload.c_str());
			}

			next_probe_at = 0;
			console::info("Hard pause: checking that everyone can pause. The game is still running.\n");
		}

		bool bound_client(game::PartyData* party_data)
		{
			const auto* conn = connection();
			return party_data && !game::Party_AreWeHost(party_data) && party_data->hostIndex < 48 && conn &&
				party_data->session->sessionId == client.current.session && match_id() == client.current.match &&
				party_data->members[party_data->hostIndex].xuid == client_host_xuid &&
				same_endpoint(conn->server, client_host);
		}

		void handle_event(const event& value, game::PartyData* party_data, const std::uint64_t now)
		{
			if (value.epoch != epoch)
			{
				return;
			}

			if (acknowledge_removal(value))
			{
				return;
			}

			const auto& msg = value.packet;
			switch (value.type)
			{
			case event_type::start:
				start_pause(party_data, now);
				break;
			case event_type::ready:
			case event_type::unready:
			{
				const bool ready = value.type == event_type::ready;
				const bool changed = value.generation && value.generation == visible_generation &&
					value.round == (host.state != phase::idle ? host.round : client.current.round) &&
					(host.state != phase::idle ? host.set_local_ready(ready) : client.set_ready(ready));
				if (changed)
				{
					console::info("Hard pause: you are %s.\n", ready ? "ready" : "not ready");
				}
				else
				{
					console::info("Hard pause: you cannot change your readiness right now.\n");
				}
				break;
			}

			case event_type::remove:
				if (!value.generation || value.generation != host.generation || value.round != host.round ||
					!removal_matches(party_data, value.slot))
				{
					console::info("Hard pause: only the host can remove a guest while the game is paused.\n");
				}
				else if (host.present(value.slot, now))
				{
					console::info("Hard pause: player %zu is still responding. Only absent players can be removed.\n", value.slot);
				}
				else
				{
					request_removal(party_data, value.slot);
				}
				break;

			case event_type::leave:
				for (std::size_t i = 0; i < protocol::capacity; ++i)
				{
					if (same_endpoint(value.from, roster[i].address) && host.confirms_leave(i, msg))
					{
						request_removal(party_data, i);
					}
				}
				break;

			case event_type::status:
			{
				std::lock_guard lock{display_mutex};
				console::info("%s\n",
					display.empty() ? "Hard pause: not active. The host can use pause_hard to pause the match."
									: display.c_str());
				if (bound_host(party_data) && protocol::is_frozen(host.state))
				{
					for (std::size_t i = 0; i < protocol::capacity; ++i)
					{
						if (host.roster & (1u << i))
						{
							console::info("  Slot %zu: %.36s - %s\n", i, host_clients[i].name,
								(host.local & (1u << i)) ? "host" : host.present(i, now) ? "present" : "absent");
						}
					}
					console::info("Hard pause: the host can use pause_hard_remove <slot> to remove an absent player.\n");
				}
				break;
			}

			case event_type::bootstrap:
			{
				const auto* conn = connection();
				if (host.state != phase::idle || !party_data || game::Party_AreWeHost(party_data) ||
					party_data->hostIndex >= 48 || !party_data->members[party_data->hostIndex].xuid || !conn ||
					!remote_address(conn->server) || msg.match != match_id() ||
					msg.session != party_data->session->sessionId || !client.begin(msg, now))
				{
					return;
				}

				client_host = conn->server;
				client_host_xuid = party_data->members[party_data->hostIndex].xuid;
				visible_generation = msg.generation;
				visible_round = msg.round;
				held_party = party_data;
				held_session = msg.session;
				console::info("Hard pause: the host is preparing to pause the match.\n");
				break;
			}

			case event_type::state:
				if (msg.state == phase::removed && same_endpoint(value.from, client_host) && client.dismiss(msg))
				{
					// Party cleanup may already have cleared membership. The immutable
					// bootstrap token still authenticates this terminal host decision.
					network::send(client_host, protocol::ack_command, protocol::encode(client.acknowledgement()));
					console::info("Hard pause: you are no longer in this match. Disconnecting.\n");
					game::Cbuf_AddText(0, "disconnect\n");
					break;
				}

				if (!bound_client(party_data) || !same_endpoint(value.from, client_host) || !client.receive(msg, now))
				{
					return;
				}

				local_pause(client.frozen());
				timeout_exempt = client.frozen();
				network::send(client_host, protocol::ack_command, protocol::encode(client.acknowledgement()));
				break;
			case event_type::recover:
			case event_type::ack:
				// Once released, a native disconnect must not prevent the other
				// original guests from acknowledging their release packets.
				if (host.state == phase::released ? !bound_host(party_data) : !roster_matches(party_data))
				{
					return;
				}

				for (std::size_t i = 0; i < protocol::capacity; ++i)
				{
					if (!host.is_guest(i) || !same_endpoint(value.from, roster[i].address))
					{
						continue;
					}

					if (value.type == event_type::ack)
					{
						host.acknowledge(i, msg, now);
					}
					else if (host.request_recovery(i, msg))
					{
						next_probe_at = 0;
					}
				}

				break;
			}
		}

		std::string status_text(const phase state, const std::uint8_t mask, const std::uint8_t ready)
		{
			switch (state)
			{
			case phase::preparing:
				return "Hard pause: checking that everyone can pause. The game is still running.";
			case phase::arming:
				return "Hard pause: pausing the match. Wait before putting your PC to sleep.";
			case phase::recovering:
				return "Hard pause: waiting for players to return. Everyone must ready up again.";
			case phase::paused:
				return utils::string::va("Hard pause: %d/%d ready. You can put your PC to sleep. Use pause_hard_ready when back.",
					std::popcount(static_cast<unsigned>(ready & mask)), std::popcount(static_cast<unsigned>(mask)));
			case phase::resuming:
				return "Hard pause: checking that everyone is ready to resume.";
			case phase::blocked:
				return "Hard pause: the players or match changed. Cannot resume. End the match.";
			case phase::removed:
				return "Hard pause: you are no longer in this match. Disconnecting.";
			case phase::cancelled:
				return "Hard pause: not all players could pause. The game is still running.";
			case phase::released:
				// Keep the completed generation for late packets, but clear the HUD
				// once gameplay resumes on either the host or a guest.
				return {};
			default:
				return {};
			}
		}

		void update_host(game::PartyData* party_data, const std::uint64_t now)
		{
			const auto previous = host.state;
			const auto state = pending_removals ? host.state : host.advance(now, roster_matches(party_data));
			if (state != previous)
			{
				next_probe_at = 0;
				const auto text = state == phase::released
					? "Hard pause: everyone is ready. Resuming the game."
					: status_text(state, host.roster, host.ready_mask());
				console::info("%s\n", text.c_str());
				if (state == phase::released || state == phase::cancelled)
				{
					terminal_at = now;
				}
			}

			visible_round = host.round;
			const bool frozen = protocol::is_frozen(state);
			server_frozen = frozen;
			local_pause(frozen);
			timeout_exempt = frozen;
			if (now >= next_probe_at)
			{
				host.issue_probe(now);
				for (std::size_t i = 0; i < protocol::capacity; ++i)
				{
					if (host.is_guest(i))
					{
						network::send(roster[i].address, protocol::state_command, protocol::encode(host.packet(i)));
					}
				}

				next_probe_at = now + protocol::heartbeat_ms;
			}

			set_display(status_text(state, host.roster, host.ready_mask()));
			// Retain release retransmission until every connected guest has received it.
			// A lost release packet must not strand a client behind a stopped sender.
			const bool complete = state == phase::released ? host.release_complete(connected_guests(party_data))
				: state == phase::cancelled && host.all_acknowledged();
			if (complete && now - terminal_at >= 1000)
			{
				// A guest that missed release may have lost its server slot. Retain
				// a dismissal so it can exit the hold when its connection returns.
				if (state == phase::released)
				{
					for (std::size_t i = 0; i < protocol::capacity; ++i)
					{
						if (host.is_guest(i) && !host.members[i].acknowledged)
						{
							auto notice = host.packet(i);
							notice.state = phase::removed;
							notice.probe = notice.value = notice.wake = 0;
							removed_peers.push_back({roster[i].address, notice});
							next_removal_at = 0;
						}
					}
				}

				clear_state(true);
			}
			else if (state == phase::cancelled && now - terminal_at > protocol::prepare_ms)
			{
				clear_state(true);
			}
		}

		void update_client(game::PartyData* party_data, const std::uint64_t now)
		{
			const bool bound = bound_client(party_data);
			local_pause(client.frozen());
			// Arming is acknowledged only after the persistent hold is installed.
			// Missing host probes now keep the match frozen instead of disconnecting.
			timeout_exempt = bound && client.frozen();
			visible_round = client.current.round;
			if (bound && client.recovery_pending() && now >= next_probe_at)
			{
				network::send(client_host, protocol::recover_command, protocol::encode(client.recovery_request()));
				next_probe_at = now + protocol::heartbeat_ms;
			}

			set_display(status_text(client.current.state, static_cast<std::uint8_t>(client.current.value >> 4),
				static_cast<std::uint8_t>(client.current.value & 15)));
			if (client.current.state == phase::preparing && now - client.heard_at > protocol::prepare_ms)
			{
				clear_state();
			}

			// Keep the completed generation as a tombstone until map/disconnect or
			// a new reliable bootstrap, rejecting delayed pause/ready/release packets.
		}

		void tick()
		{
			if (!accepting)
			{
				return;
			}

			const auto now = now_ms();
			const bool has_state = host.state != phase::idle || client.current.generation;
			if (!removed_peers.empty() && (!playing_zombies() || removed_peers.front().notice.match != match_id()))
			{
				removed_peers.clear();
			}
			if (has_state &&
				(!playing_zombies() || (host.state != phase::idle ? host.match : client.current.match) != match_id()))
			{
				clear_state();
				return;
			}

			std::deque<event> events;
			{
				std::lock_guard lock{inbox_mutex};
				events.swap(inbox);
			}

			if (!has_state && events.empty() && removed_peers.empty())
			{
				return;
			}

			// The native barrier drains outstanding server work and disables server
			// packet processing (0x6E1A00 -> 0x675FE0/0x6751A0). Read client slots
			// and enqueue reliable bootstrap commands only inside this main-thread boundary.
			if (playing_zombies() && party::server_running() && game::SV_Loaded())
			{
				utils::hook::invoke<void>(0x6E1A00_g);
			}

			auto* party_data = private_party();
			std::erase_if(removed_peers, [party_data](const removed_peer& peer)
			{
				return !party_data || !game::Party_AreWeHost(party_data) ||
					party_data->session->sessionId != peer.notice.session;
			});

			const auto power = power_generation.load();
			const bool interrupted = power != seen_power_generation ||
				(last_tick_at && (now < last_tick_at || now - last_tick_at > protocol::presence_ms));
			seen_power_generation = power;
			last_tick_at = now;

			if (interrupted && has_state)
			{
				if (host.state != phase::idle)
				{
					host.recover();
				}
				else
				{
					client.recover();
				}

				next_probe_at = 0;
				// Discard packets and commands queued before the power transition.
				events.clear();
			}

			finish_removals(party_data);
			if (bound_host(party_data))
			{
				for (std::size_t i = 0; i < protocol::capacity; ++i)
				{
					// The native party disconnectMessage sets reason 1 (0x4885F8).
					// Timeout reasons are not departures; sleepers keep their places.
					if (host.can_remove(i) && (host_clients[i].state == client_zombie ||
						utils::hook::invoke<unsigned int>(0x6DD630_g, &host_clients[i]) == 1))
					{
						request_removal(party_data, i);
					}
				}
			}

			client.check_presence(now);
			// Confirmed departure notices precede strict roster validation: the
			// native party may already have started clearing that member.
			for (const auto& value : events)
			{
				if (value.type == event_type::leave)
				{
					handle_event(value, party_data, now);
				}
			}

			if (host.state != phase::idle)
			{
				host.check_presence(now, roster_matches(party_data));
			}

			for (const auto& value : events)
			{
				if (value.type != event_type::leave)
				{
					handle_event(value, party_data, now);
				}
			}

			if (now >= next_removal_at)
			{
				for (const auto& peer : removed_peers)
				{
					network::send(peer.address, protocol::state_command, protocol::encode(peer.notice));
				}
				next_removal_at = now + protocol::heartbeat_ms;
			}

			if (host.state != phase::idle)
			{
				update_host(party_data, now);
			}
			else if (client.current.generation)
			{
				update_client(party_data, now);
			}
		}

		void draw_status()
		{
			if (!accepting || !playing_zombies())
			{
				return;
			}

			std::string text;
			{
				std::lock_guard lock{display_mutex};
				text = display;
			}

			if (text.empty())
			{
				return;
			}

			auto* font = game::R_RegisterFont("fonts/fira_mono_regular.ttf", 16);
			const auto* placement = game::ScrPlace_GetViewPlacement(game::LOCAL_CLIENT_0);
			if (!font || !placement)
			{
				return;
			}

			const float color[]{1.0f, 0.85f, 0.3f, 1.0f};
			const auto width = game::R_TextWidthWithFlags(text.c_str(), 0, font, 0, 0, font->pixelHeight, 0.0f, 1);
			const auto x = placement->realViewportPosition[0] + (placement->realViewportSize[0] - width) * 0.5f;
			const auto y = placement->realViewportPosition[1] + placement->realViewportSize[1] - 30.0f;
			game::R_AddCmdDrawText(text.c_str(), std::numeric_limits<int>::max(), font, 0, 0, font->pixelHeight,
				x, y, 1.0f, 1.0f, 0.0f, color, game::R_Font_GetLegacyFontStyle(6));
		}

		bool hold_server()
		{
			return server_frozen.load();
		}

		bool hold_client(const int local_client)
		{
			return !local_client && client_frozen.load();
		}

		bool hold_timeout(const int local_client, const int state)
		{
			return !local_client && state == 10 && timeout_exempt.load();
		}

		void install_pause_branches()
		{
			// Only replace the 7-byte global load at each site. Preserve all GPRs
			// across the predicate and replay the original load on the native path.
			// These predicates use no engine calls or floating-point state.
			const auto server_stub = utils::hook::assemble([](utils::hook::assembler& a)
			{
				const auto paused = a.new_label();
				a.pushad64();
				a.call_aligned(hold_server);
				a.test(al, al);
				a.jnz(paused);
				a.popad64();
				a.mov(rax, 0x1BD37B0_g);
				a.mov(rax, qword_ptr(rax));
				a.jmp(0x6DF58D_g);
				a.bind(paused);
				a.popad64();
				// Native sv_paused setter, pause-resume marker, profiling, server
				// synchronization and Com_CheckError. No accumulator/tick increment.
				a.jmp(0x6DF684_g);
			});
			utils::hook::nop(0x6DF586_g, 7);
			utils::hook::jump(0x6DF586_g, server_stub);

			const auto client_stub = utils::hook::assemble([](utils::hook::assembler& a)
			{
				const auto paused = a.new_label();
				a.pushad64();
				a.mov(ecx, edi);
				a.call_aligned(hold_client);
				a.test(al, al);
				a.jnz(paused);
				a.popad64();
				a.mov(rax, 0x1BD37B8_g);
				a.mov(rax, qword_ptr(rax));
				a.jmp(0x86E8F_g);
				a.bind(paused);
				a.popad64();
				// CL_SetCGameTime's native pause exit; called after packet parsing.
				a.jmp(0x8703E_g);
			});
			utils::hook::nop(0x86E88_g, 7);
			utils::hook::jump(0x86E88_g, client_stub);

			const auto timeout_stub = utils::hook::assemble([](utils::hook::assembler& a)
			{
				const auto paused = a.new_label();
				a.pushad64();
				a.mov(ecx, edi);
				a.mov(edx, ebp);
				a.call_aligned(hold_timeout);
				a.test(al, al);
				a.jnz(paused);
				a.popad64();
				a.mov(rcx, 0x1BD37B0_g);
				a.mov(rcx, qword_ptr(rcx));
				a.jmp(0x7549E_g);
				a.bind(paused);
				a.popad64();
				// Only CA_ACTIVE and our established hold. Native connect/loading
				// checks survive; an absent host cannot cancel an armed sleep hold.
				a.jmp(0x7553A_g);
			});
			utils::hook::nop(0x75497_g, 7);
			utils::hook::jump(0x75497_g, timeout_stub);
		}

		void party_frame_stub(game::PartyData* party_data, const unsigned int controller, const int msec)
		{
			// Live_Frame calls Party_Frame for each lobby and the social party.
			// Preserve only the bound game lobby, including its migration state.
			// Explicit disconnect/map/shutdown paths still run their normal cleanup.
			if (client_frozen && party_data == held_party && party_data->session &&
				party_data->session->sessionId == held_session)
			{
				return;
			}

			party_frame_hook.invoke<void>(party_data, controller, msec);
		}

		void session_timeout_stub(const unsigned int local_client)
		{
			// CL's session/ping path has a second call to the same timeout handler,
			// before the ordinary packet-timeout branch (0x4615D9 -> 0x86BD0).
			if (local_client || !timeout_exempt)
			{
				utils::hook::invoke<void>(0x86BD0_g, local_client);
			}
		}

		int check_network_stub(const unsigned int controller)
		{
			// The Live_Frame check otherwise tears down every party before throwing
			// XBOXLIVE_NETCONNECTION while the network adapter is still waking up.
			return !controller && client_frozen ? 0 : utils::hook::invoke<int>(0x857B00_g, controller);
		}

		void shutdown_stub(const char* reason)
		{
			clear_state();
			shutdown_hook.invoke<void>(reason);
		}

		void restart_stub(const int migrate, const int load_scripts)
		{
			clear_state();
			restart_hook.invoke<void>(migrate, load_scripts);
		}

		void spawn_stub(char* map, const int preloaded, const int restart, const unsigned int migrate)
		{
			clear_state();
			spawn_hook.invoke<void>(map, preloaded, restart, migrate);
		}
	}

	bool receive(const unsigned int local_client, const command::params& args)
	{
		if (!args.size() || std::string_view{args[0]} != protocol::bootstrap_command)
		{
			return false;
		}

		if (!local_client && args.size() == 2 && accepting)
		{
			if (const auto packet = protocol::decode(args[1]))
			{
				enqueue({event_type::bootstrap, {}, *packet});
			}
		}

		return true;
	}

	void disconnect()
	{
		if (accepting)
		{
			if (host.state == phase::idle && client.frozen() && remote_address(client_host))
			{
				const auto notice = protocol::encode(client.leave_notice());
				for (int i = 0; i < 3; ++i)
				{
					network::send(client_host, protocol::leave_command, notice);
				}
			}

			clear_state();
		}
	}

	void key_event(const int local_client, const int key, const int down, const int catchers_before)
	{
		// CL_SetPaused adds catcher 0x10 without opening a menu. CL_KeyEvent's
		// Escape branch (0x45DEEE) then consumes Escape without dispatching to LUI.
		// After native key bookkeeping, reproduce its active-game menu request
		// (0x45DF50), but only when the pause catcher alone owns input. In particular,
		// an Escape that closed an existing LUI menu must not reopen it here.
		if (accepting && client_frozen && !local_client && key == 27 && down && catchers_before == 0x10 &&
			*game::keyCatchers == 0x10)
		{
			utils::hook::invoke<std::int64_t>(0x747DF0_g, 0u, 2); // UI_SetActiveMenu
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::is_zombies() || game::environment::is_dedicated())
			{
				return;
			}

			DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS subscription{power_callback, nullptr};
			const auto error = PowerRegisterSuspendResumeNotification(DEVICE_NOTIFY_CALLBACK, &subscription, &power_notification);
			if (error != ERROR_SUCCESS)
			{
				console::error("Hard pause: unavailable. Windows sleep notifications failed (error %lu).\n", error);
				return;
			}

			seen_power_generation = power_generation;
			accepting = true;
			install_pause_branches();

			shutdown_hook.create(0x6DBF50_g, shutdown_stub);
			restart_hook.create(0x6D6F60_g, restart_stub);
			spawn_hook.create(0x6DC350_g, spawn_stub);
			party_frame_hook.create(0x47C7B0_g, party_frame_stub);

			utils::hook::call(0x4615D9_g, session_timeout_stub);
			utils::hook::call(0x788D00_g, check_network_stub);
			utils::hook::call(0x6E08FD_g, server_network_done_stub);

			command::add("pause_hard", [] { enqueue_command(event_type::start); });
			command::add("pause_hard_ready", [] { enqueue_command(event_type::ready); });
			command::add("pause_hard_unready", [] { enqueue_command(event_type::unready); });
			command::add("pause_hard_status", [] { enqueue_command(event_type::status); });

			command::add("pause_hard_remove", [](const command::params& args)
			{
				std::size_t slot{};
				const std::string_view text{args.size() == 2 ? args[1] : ""};
				const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), slot);
				if (text.empty() || error != std::errc{} || end != text.data() + text.size() || slot >= protocol::capacity)
				{
					console::info("Hard pause: use pause_hard_remove <slot>. Find the slot with pause_hard_status.\n");
					return;
				}
				enqueue_command(event_type::remove, slot);
			});

			network::on(protocol::state_command, [](const game::netadr_s& from, const std::string_view& data)
			{
				enqueue_packet(event_type::state, from, data);
			});

			network::on(protocol::ack_command, [](const game::netadr_s& from, const std::string_view& data)
			{
				enqueue_packet(event_type::ack, from, data);
			});

			network::on(protocol::recover_command, [](const game::netadr_s& from, const std::string_view& data)
			{
				enqueue_packet(event_type::recover, from, data);
			});

			network::on(protocol::leave_command, [](const game::netadr_s& from, const std::string_view& data)
			{
				enqueue_packet(event_type::leave, from, data);
			});

			scheduler::loop(tick, scheduler::pipeline::main);
			scheduler::loop(draw_status, scheduler::pipeline::renderer);
		}

		void pre_destroy() override
		{
			accepting = false;
			if (power_notification)
			{
				PowerUnregisterSuspendResumeNotification(power_notification);
				power_notification = nullptr;
			}

			server_frozen = client_frozen = timeout_exempt = false;
			// Engine teardown may already have run. Never touch engine dvars here.
		}
	};
}

REGISTER_COMPONENT(pause_hard::component)
