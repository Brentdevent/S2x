#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "steam_invites.hpp"
#include "steam_proxy.hpp"
#include "auth.hpp"
#include "command.hpp"
#include "console/console.hpp"
#include "mode_switch.hpp"
#include "party.hpp"
#include "scheduler.hpp"
#include "upnp.hpp"

#include "game/party_invite.hpp"
#include "game/ui_scripting/execution.hpp"
#include "ui_scripting.hpp"
#include "steam/steam.hpp"

#include <utils/flags.hpp>
#include <utils/finally.hpp>
#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace steam_invites
{
	namespace
	{
		utils::hook::detour can_invite_friend_hook;
		utils::hook::detour can_invite_session_hook;
		const game::dvar_t* join_enabled{};
		bool stopping{};
		std::optional<std::string> published_connect;
		bool published_identity{};
		std::string invite_session;
		std::unordered_map<std::uint64_t, std::uint64_t> sent_invites;
		std::uint64_t join_generation{};

		struct pending_join
		{
			party::invite::ticket ticket;
			std::uint64_t generation{};
			std::uint64_t attempt_id{};
			std::string previous_session;
			std::string previous_private_session;
		};

		std::optional<pending_join> confirmation;

		// Native Social > Friends helpers, shared by MP and Zombies.
		const game::symbol<bool(int)> friends_available{0x7869B0};
		const game::symbol<int(int, std::uint64_t)> friend_index{0x84BBE0};
		const game::symbol<int(const game::SessionData*)> invites_blocked{0x827570};
		const game::symbol<int(const game::SessionData*)> invite_only{0x8275A0};

		std::uint64_t unix_time()
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		}

		bool enabled()
		{
			return !stopping && join_enabled && join_enabled->current.enabled;
		}

		bool already_joined(const party::invite::ticket& ticket)
		{
			return ticket.mode == (game::environment::is_zombies() ? "zm" : "mp")
				&& party::has_session(ticket.session_id);
		}

		bool needs_confirmation()
		{
			const auto* private_party = game::Party_GetPrivatePartyData();
			const auto* game_party = game::Lobby_GetPartyData(0);

			// 78A0A0 checks the social party. Include the Custom Match party and
			// gameplay too, before an invitation can disconnect or switch modes.
			return (private_party && utils::hook::invoke<bool>(0x49EBB0_g, private_party))
				|| (game_party && utils::hook::invoke<bool>(0x49EBB0_g, game_party))
				|| (!game::virtual_lobby_loaded() && game::CL_IsLocalClientInGame(0));
		}

		steam_id friend_for_guid(const std::uint64_t guid)
		{
			const auto client = steam_proxy::access_client();

			if (!enabled() || !client.friends || !guid)
			{
				return {};
			}

			const auto count = client.friends->GetFriendCount(4);

			for (auto index = 0; index < count; ++index)
			{
				steam_id id{};
				client.friends->GetFriendByIndex(&id, index, 4);

				// This association comes from the friend's Steam presence, not a packet
				// claiming to own a Steam ID. Gameplay keeps its existing S2x identity.
				// Game activity can be hidden independently of friendship; the current
				// app's rich-presence identity is sufficient to associate this friend.
				const auto* value = client.friends->GetFriendRichPresence(id, "s2x_guid");
				const std::string_view text{value ? value : "", value ? strnlen_s(value, 17) : 0};
				std::uint64_t friend_guid{};

				if (party::invite::valid_session(text)
					&& std::from_chars(text.data(), text.data() + text.size(), friend_guid, 16).ec == std::errc{}
					&& friend_guid == guid)
				{
					return id;
				}
			}

			return {};
		}

		bool is_friend(const int controller, const std::uint64_t guid)
		{
			party::session::connect_info info{};

			if (!party::get_invite_connect_info(info))
			{
				return utils::hook::invoke<bool>(0x84BD20_g, controller, guid);
			}

			if (!friend_for_guid(guid).bits)
			{
				console::warn("[steam] Friends Only join rejected: no Steam friend advertises S2x identity %016llx.\n", guid);
				return false;
			}

			return true;
		}

		bool was_invited(const int controller, const std::uint64_t guid)
		{
			party::session::connect_info info{};
			auto* party_data = party::get_invite_connect_info(info);

			if (!party_data)
			{
				return utils::hook::invoke<bool>(0x84B830_g, controller, guid);
			}

			const auto* session = utils::hook::invoke<const game::SessionData*>(0x470F50_g, party_data);

			if (!enabled() || !session || invites_blocked(session) || info.session.session_id != invite_session)
			{
				return false;
			}

			const auto invite = sent_invites.find(friend_for_guid(guid).bits);
			if (invite == sent_invites.end() || invite->second <= unix_time())
			{
				console::warn("[steam] Invite Only join rejected: no current Steam invite for S2x identity %016llx.\n", guid);
				return false;
			}

			return true;
		}

		void clear_stale_invites()
		{
			party::session::connect_info info{};
			auto* party_data = party::get_invite_connect_info(info);
			const auto* session = party_data
				? utils::hook::invoke<const game::SessionData*>(0x470F50_g, party_data) : nullptr;

			if (!enabled() || !session || invites_blocked(session)
				|| info.session.session_id != invite_session)
			{
				invite_session.clear();
				sent_invites.clear();
			}

			const auto now = unix_time();
			std::erase_if(sent_invites, [now](const auto& invite) { return invite.second <= now; });

			if (confirmation && (!enabled() || confirmation->generation != join_generation
				|| confirmation->ticket.expires <= now
				|| !party::is_connection_attempt_current(confirmation->attempt_id)
				|| confirmation->previous_session != party::get_current_session_id()
				|| confirmation->previous_private_session != party::get_current_session_id(1)))
			{
				confirmation.reset();
			}
		}

		std::optional<party::invite::ticket> host_ticket(const bool advertise = false)
		{
			if (!enabled())
			{
				return {};
			}

			party::session::connect_info info{};
			auto* party_data = party::get_invite_connect_info(info);

			if (!party_data || info.player_count >= info.max_players)
			{
				return {};
			}

			const auto* session = utils::hook::invoke<const game::SessionData*>(0x470F50_g, party_data);

			// Check the session's invite-only flag directly: lobby_privacy returns
			// a public-match value for map sessions with privateMatch cleared.
			if (!session || invites_blocked(session) || (advertise && invite_only(session)))
			{
				return {};
			}

			const auto endpoint = upnp::get_endpoint(info.session.session_id);

			if (endpoint.empty())
			{
				return {};
			}

			return party::invite::ticket{game::environment::is_zombies() ? "zm" : "mp", endpoint,
				info.session.session_id, unix_time() + party::invite::lifetime_seconds};
		}

		void perform_join(const party::invite::ticket& ticket)
		{
			if (!enabled() || ticket.expires <= unix_time() || already_joined(ticket))
			{
				return;
			}

			if (game::is_local_play())
			{
				console::warn("[steam] Switch to online play before joining a party.\n");
				return;
			}

			const auto mode = ticket.mode == "zm" ? game::environment::mode::zombies : game::environment::mode::multiplayer;

			if (mode != game::environment::get_mode())
			{
				mode_switch::join_invite(mode, party::invite::encode(ticket));
				return;
			}

			party::queue_session_join(ticket.endpoint, ticket.session_id, ticket.expires, true);
		}

		void confirm_join(const std::uint64_t generation, const bool accepted)
		{
			// Leave LUI before disconnecting: the native disconnect can longjmp.
			scheduler::once([generation, accepted]
			{
				if (!confirmation || confirmation->generation != generation || generation != join_generation)
				{
					return;
				}

				auto request = std::move(*confirmation);
				confirmation.reset();

				if (accepted && party::is_connection_attempt_current(request.attempt_id)
					&& request.previous_session == party::get_current_session_id()
					&& request.previous_private_session == party::get_current_session_id(1))
				{
					perform_join(request.ticket);
				}
			}, scheduler::main);
		}

		bool show_confirmation(const std::uint64_t generation)
		{
			if (!*game::hks::lui_lua_state)
			{
				return false;
			}

			game::LUI_EnterCriticalSection();
			const auto unlock = utils::finally(game::LUI_LeaveCriticalSection);

			try
			{
				const auto lua = ui_scripting::get_globals();
				const auto engine = lua.get("Engine").as<ui_scripting::table>();
				const auto flow = lua.get("LUI").as<ui_scripting::table>().get("FlowManager").as<ui_scripting::table>();
				const auto types = lua.get("ModalUtils").as<ui_scripting::table>()
					.get("NotificationModalType").as<ui_scripting::table>();
				ui_scripting::table properties;
				properties["titleText"] = engine.get("Localize")("@MENU_WARNING")[0];
				properties["descText"] = "Leave your current party to join this party?";
				properties["modalType"] = types.get("GeneralNotifications");
				properties["accept_func"] = [generation] { confirm_join(generation, true); };
				properties["cancel_func"] = [generation] { confirm_join(generation, false); };
				properties["choices"] = ui_scripting::table{};
				flow.get("RequestAddMenu")(engine.get("GetLuiRoot")()[0], "notification_modal", true,
					engine.get("GetFirstActiveController")()[0], false, properties);
				return true;
			}
			catch (const std::exception& e)
			{
				console::warn("[steam] Unable to show the leave-party confirmation: %s\n", e.what());
				return false;
			}
		}

		void queue_invite(const party::invite::ticket& ticket)
		{
			const auto generation = ++join_generation;
			const auto attempt_id = party::get_connection_attempt_id();

			// Steam's callback and the native Friends action must return before any
			// disconnect/quit. Consume this task before a possible native longjmp.
			scheduler::once([ticket, generation, attempt_id]
			{
				if (!enabled() || generation != join_generation || !party::is_connection_attempt_current(attempt_id))
				{
					return;
				}

				++join_generation;
				confirmation.reset();

				if (ticket.expires <= unix_time() || already_joined(ticket) || game::is_local_play())
				{
					return;
				}

				if (needs_confirmation())
				{
					confirmation = pending_join{ticket, join_generation, attempt_id,
						party::get_current_session_id(), party::get_current_session_id(1)};

					if (!show_confirmation(join_generation))
					{
						confirmation.reset();
					}

					return;
				}

				perform_join(ticket);
			}, scheduler::main);
		}

		std::optional<party::invite::ticket> friend_ticket(const std::uint64_t friend_id)
		{
			const auto client = steam_proxy::access_client();
			steam_id id{};
			id.bits = friend_id;
			steam::friend_game_info game_info{};

			if (!enabled() || !client.friends || !client.friends->HasFriend(id, 4)
				|| !client.friends->GetFriendGamePlayed(id, &game_info) || game_info.game_id != 476620)
			{
				return {};
			}

			const auto* value = client.friends->GetFriendRichPresence(id, "connect");
			const std::string_view connect{value ? value : "", value ? strnlen_s(value, 256) : 0};

			return party::invite::parse_steam_connect(connect, unix_time());
		}

		bool can_join_friend(const int controller, const std::uint64_t friend_id, const char** error)
		{
			*error = "MENU_JOIN_SESSION_INVALID";

			// The callers still resolve the selected friend through the native array.
			// Require its membership before they index that array (84B1A0 / 84BA40).
			if (!enabled() || game::is_local_play() || !friends_available(controller)
				|| friend_index(controller, friend_id) < 0)
			{
				return false;
			}

			const auto ticket = friend_ticket(friend_id);

			if (!ticket)
			{
				return false;
			}

			if (already_joined(*ticket))
			{
				*error = "MENU_FRIEND_ALREADY_CONNECTED";
				return false;
			}

			*error = nullptr;
			return true;
		}

		bool join_friend(const int, const std::uint64_t friend_id, const std::uint64_t, const unsigned char)
		{
			const auto ticket = friend_ticket(friend_id);

			if (!ticket || already_joined(*ticket))
			{
				return false;
			}

			queue_invite(*ticket);
			return true;
		}

		bool can_invite_friend(const int controller, const std::uint64_t friend_id, const char** error)
		{
			if (!can_invite_friend_hook.invoke<bool>(controller, friend_id, error))
			{
				return false;
			}

			if (!host_ticket())
			{
				*error = "MENU_INVITE_SESSION_INVALID";
				return false;
			}

			return true;
		}

		bool can_invite_session(const int controller, const char** error)
		{
			if (!can_invite_session_hook.invoke<bool>(controller, error))
			{
				return false;
			}

			if (!host_ticket())
			{
				*error = "MENU_INVITE_SESSION_INVALID";
				return false;
			}

			return true;
		}

		void update_presence()
		{
			if (stopping)
			{
				return;
			}

			clear_stale_invites();
			const auto client = steam_proxy::access_client();

			if (!client.friends)
			{
				published_connect.reset();
				published_identity = false;
				invite_session.clear();
				sent_invites.clear();
				confirmation.reset();
				return;
			}

			if (!published_identity)
			{
				published_identity = client.friends->SetRichPresence("s2x_guid",
					utils::string::va("%016llx", auth::get_guid()));
			}

			const auto ticket = host_ticket(true);
			std::string connect;

			if (ticket)
			{
				const auto previous = published_connect
					? party::invite::parse_steam_connect(*published_connect, unix_time()) : std::nullopt;

				if (previous && previous->mode == ticket->mode && previous->endpoint == ticket->endpoint
					&& previous->session_id == ticket->session_id && previous->expires + 60 > ticket->expires)
				{
					return;
				}

				connect = std::string{party::invite::steam_connect_prefix} + party::invite::encode(*ticket);
			}

			if (published_connect && *published_connect == connect)
			{
				return;
			}

			if (client.friends->SetRichPresence("connect", connect.empty() ? nullptr : connect.c_str()))
			{
				client.friends->SetRichPresence("status", ticket ? "S2x Party" : "S2x");
				published_connect = std::move(connect);
			}
		}
	}

	bool invite_friend(const std::uint64_t friend_id)
	{
		if (!game::environment::uses_multiplayer_binary() || game::environment::is_dedicated())
		{
			return false;
		}

		const auto ticket = host_ticket();
		const auto client = steam_proxy::access_client();
		steam_id id{};
		id.bits = friend_id;

		if (!ticket || !client.friends || !client.friends->HasFriend(id, 4))
		{
			return false;
		}

		client.friends->RequestFriendRichPresence(id);
		const auto connect = std::string{party::invite::steam_connect_prefix} + party::invite::encode(*ticket);

		if (!client.friends->InviteUserToGame(id, connect.c_str()))
		{
			return false;
		}

		if (invite_session != ticket->session_id)
		{
			sent_invites.clear();
			invite_session = ticket->session_id;
		}

		sent_invites[friend_id] = ticket->expires;
		return true;
	}

	void accept_invite(const std::string_view connect_string)
	{
		if (!enabled())
		{
			return;
		}

		const auto ticket = party::invite::parse_steam_connect(connect_string, unix_time());

		if (!ticket)
		{
			console::warn("[steam] Ignoring an invalid or expired S2x invite.\n");
			return;
		}

		queue_invite(*ticket);
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated())
			{
				return;
			}

			command::add("steam_invite_status", []
			{
				console::info("[steam] S2x identity %016llx, native identity %016llx.\n", auth::get_guid(),
					utils::hook::invoke<std::uint64_t>(0x789870_g, game::CL_ControllerIndexFromClientNum(0)));
				const auto client = steam_proxy::access_client();
				if (!client.friends)
				{
					console::info("[steam] Friends service is unavailable.\n");
					return;
				}

				for (auto index = 0; index < client.friends->GetFriendCount(4); ++index)
				{
					steam_id id{};
					steam::friend_game_info game_info{};
					client.friends->GetFriendByIndex(&id, index, 4);
					if (client.friends->GetFriendGamePlayed(id, &game_info) && game_info.game_id == 476620)
					{
						const auto* guid = client.friends->GetFriendRichPresence(id, "s2x_guid");
						console::info("[steam] Friend %016llx: S2x identity %.16s.\n", id.bits,
							guid && *guid ? guid : "<unavailable>");
						client.friends->RequestFriendRichPresence(id);
					}
				}
			});

			// Keep native menu selection, errors and invite cooldown.
			// Only replace Steam lobby joinability / joining with S2x ticket handling.
			utils::hook::jump(0x84B900_g, can_join_friend);
			utils::hook::jump(0x786D40_g, join_friend);
			can_invite_friend_hook.create(0x84B6C0_g, can_invite_friend);
			can_invite_session_hook.create(0x84B580_g, can_invite_session);

			// PartyHost admission and reserved-slot checks use gameplay XUIDs.
			// Bridge only these calls; the Social menu continues to use Steam IDs.
			utils::hook::call(0x4866A1_g, is_friend);
			utils::hook::call(0x488F7B_g, is_friend);
			utils::hook::call(0x4866CE_g, was_invited);
			utils::hook::call(0x488F95_g, was_invited);

			ui_scripting::on_start([]
			{
				scheduler::once([] { confirmation.reset(); }, scheduler::main);
			});

			scheduler::once([]
			{
				join_enabled = game::Dvar_RegisterBool("steam_join", true, game::DVAR_FLAG_SAVED);

				if (const auto secret = utils::flags::get_value("-s2x-invite"))
				{
					accept_invite(std::string{party::invite::steam_connect_prefix} + *secret);
				}
			}, scheduler::main);

			scheduler::loop(update_presence, scheduler::main, 250ms);
		}

		void pre_destroy() override
		{
			stopping = true;
			published_connect.reset();
			published_identity = false;
			invite_session.clear();
			sent_invites.clear();
			confirmation.reset();
			++join_generation;
			// SteamAPI_Shutdown / steam_proxy clear our keys before releasing the client.
		}
	};
}

REGISTER_COMPONENT(steam_invites::component)
