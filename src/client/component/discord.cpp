#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "console/console.hpp"
#include "mode_switch.hpp"
#include "party.hpp"
#include "scheduler.hpp"
#include "upnp.hpp"

#include "game/mode_switch_arguments.hpp"
#include "game/party_invite.hpp"
#include "game/player_profile.hpp"
#include "game/types/discord.hpp"

#include <utils/finally.hpp>
#include <utils/flags.hpp>
#include <utils/hook.hpp>
#include <utils/nt.hpp>
#include <utils/string.hpp>

namespace discord
{
	namespace
	{
		using namespace game::native_discord;

		constexpr auto application_id = "1569752950874578984";

		utils::hook::detour initialize_hook;
		utils::hook::detour presence_hook;
		utils::hook::detour shutdown_hook;
		utils::hook::detour respond_hook;
		utils::hook::detour respond_to_request_hook;
		utils::hook::detour respond_to_all_requests_hook;
		utils::hook::detour serialize_reply_hook;

		const game::symbol<discord_presence> native_presence{0x6E8F470};
		const game::symbol<discord_presence> native_last_presence{0x6E8F4E0};
		const game::symbol<void()> clear_requests{0x1E1B80};
		const game::symbol<int(int)> profile_presence{0x46A5E0};
		const game::symbol<bool()> profile_allows_join{0x1E5980};
		const game::symbol<int(const game::SessionData*)> invites_blocked{0x827570};
		const game::symbol<std::uint32_t()> host_invite_token{0x1E4A10};
		const game::symbol<long(const wchar_t*, const wchar_t*)> register_launch{0x1997B0, 0x39050};

		const game::dvar_t* join_enabled{};
		bool initialized{};
		bool stopping{};
		discord_handlers stock_handlers{};

		std::string advertised_session;
		std::string advertised_endpoint;
		std::uint32_t advertised_token{};
		std::string join_secret;
		std::string last_signature;
		std::uint64_t refresh_at{};

		void register_settings()
		{
			if (join_enabled)
			{
				return;
			}

			join_enabled = game::Dvar_RegisterBool("discord_join", true, game::DVAR_FLAG_SAVED);
		}

		std::uint64_t unix_time()
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		}

		bool can_invite(party::session::connect_info& info, std::string& endpoint)
		{
			const auto* post_secret = game::Dvar_FindMalleableVar("744");

			if (stopping || !initialized || !join_enabled->current.enabled
				|| !post_secret || !post_secret->current.enabled || profile_presence(0) != 1
				|| !profile_allows_join())
			{
				return false;
			}

			auto* party_data = party::get_invite_connect_info(info);
			if (!party_data || info.player_count >= info.max_players)
			{
				return false;
			}

			const auto* session = utils::hook::invoke<const game::SessionData*>(0x470F50_g, party_data);

			if (!session || invites_blocked(session) || !host_invite_token())
			{
				return false;
			}

			endpoint = upnp::get_endpoint(info.session.session_id);

			return !endpoint.empty();
		}

		bool can_join_advertised_session()
		{
			party::session::connect_info info{};
			std::string endpoint;

			return can_invite(info, endpoint) && info.session.session_id == advertised_session
				&& endpoint == advertised_endpoint && host_invite_token() == advertised_token;
		}

		discord_presence adapt_presence(const discord_presence& source)
		{
			auto value = source;

			// Preserve native artwork, with a round S2x badge. The generic logo
			// already carries our branding, so it does not need a second badge.
			if (value.large_image && (std::strcmp(value.large_image, "s2_dil_app_logo") == 0
				|| std::strcmp(value.large_image, "s2_dil_default") == 0))
			{
				value.large_image = "s2x";
				value.large_text = "S2x";
				value.small_image = nullptr;
				value.small_text = nullptr;
			}
			else if (value.small_image && *value.small_image)
			{
				value.small_image = "s2x_round";
				value.small_text = "S2x";
			}

			// Retail Steam lobby secrets do not apply to S2x Custom Matches.
			value.party_id = nullptr;
			value.party_size = value.party_max = 0;
			value.match_secret = value.join_secret = value.spectate_secret = nullptr;

			if (game::environment::is_singleplayer())
			{
				return value;
			}

			party::session::connect_info info{};
			std::string endpoint;
			const auto joinable = can_invite(info, endpoint);
			const auto session = joinable ? info.session.session_id : std::string{};
			const auto token = joinable ? host_invite_token() : 0;

			if (session != advertised_session || endpoint != advertised_endpoint || token != advertised_token)
			{
				// Requests belong to one advertised session; do not offer an old
				// request after leave, host migration, a port change or privacy change.
				clear_requests();
				advertised_session = session;
				advertised_endpoint = endpoint;
				advertised_token = token;
				refresh_at = 0;
				join_secret.clear();
			}

			if (!joinable)
			{
				return value;
			}

			const auto now = unix_time();

			if (now >= refresh_at)
			{
				join_secret = party::invite::encode({game::environment::is_zombies() ? "zm" : "mp",
					endpoint, session, now + party::invite::lifetime_seconds, token});
				refresh_at = now + 60;
			}

			value.party_id = advertised_session.c_str();
			value.party_size = info.player_count;
			value.party_max = info.max_players;
			value.join_secret = join_secret.c_str();

			return value;
		}

		int presence_stub(const discord_presence* source)
		{
			if (!initialized || !source)
			{
				return 0;
			}

			const auto value = adapt_presence(*source);

			// Native UpdatePresence serializes synchronously before waking its IO
			// thread (MP 19ABD0 / SP 3A3E0); these borrowed pointers do not escape.
			return presence_hook.invoke<int>(&value);
		}

		void accept_invite(const char* secret)
		{
			if (stopping || !secret || !join_enabled->current.enabled)
			{
				return;
			}

			const auto length = strnlen_s(secret, party::invite::max_secret_length + 1);
			const std::string_view secret_view{secret, length};
			const auto invite = party::invite::parse(secret_view, unix_time());

			if (!invite)
			{
				console::warn("[discord] Ignoring an invalid or expired S2x invite.\n");
				return;
			}

			const auto mode = invite->mode == "zm" ? game::environment::mode::zombies
				: game::environment::mode::multiplayer;

			if (mode != game::environment::get_mode())
			{
				mode_switch::join_invite(mode, secret_view, true);
				return;
			}

			if (game::is_local_play())
			{
				console::warn("[discord] Switch to online play before joining a Custom Match.\n");
				return;
			}

			// Native RunCallbacks is called from the game's frame (1E2180).
			// queue_session_join owns readiness, timeout and cancellation on main.
			party::queue_session_join(invite->endpoint, invite->session_id, invite->expires, false, invite->discord_token);
		}

		auto use_advertised_join_secret()
		{
			// The native notification and both UI response handlers check this
			// pointer. Scope the substitution so stock presence keeps its ownership.
			const auto previous = native_last_presence->join_secret;
			native_last_presence->join_secret = can_join_advertised_session() ? join_secret.c_str() : nullptr;

			return utils::finally([previous]
			{
				native_last_presence->join_secret = previous;
			});
		}

		void request_join(const void* request)
		{
			if (!request)
			{
				return;
			}

			const auto restore = use_advertised_join_secret();

			if (!native_last_presence->join_secret)
			{
				// The native inline user payload starts with its NUL-terminated ID.
				respond_hook.invoke<int>(request, 2);
				return;
			}

			if (stock_handlers.join_request)
			{
				stock_handlers.join_request(request);
			}
		}

		void respond_to_request_stub(const char* user_id, const int reply, const int slot)
		{
			const auto restore = use_advertised_join_secret();
			respond_to_request_hook.invoke<void>(user_id, reply, slot);
		}

		std::intptr_t respond_to_all_requests_stub(const int reply)
		{
			const auto restore = use_advertised_join_secret();
			return respond_to_all_requests_hook.invoke<std::intptr_t>(reply);
		}

		int respond_stub(const char* user_id, int reply)
		{
			if (reply == 1 && !can_join_advertised_session())
			{
				reply = 2;
			}

			return respond_hook.invoke<int>(user_id, reply);
		}

		std::size_t serialize_reply_stub(char* buffer, const std::size_t capacity,
			const char* user_id, const int reply, const int nonce)
		{
			const auto length = serialize_reply_hook.invoke<std::size_t>(buffer, capacity, user_id, reply, nonce);

			// This embedded RPC predates the current CLOSE_ACTIVITY_REQUEST name.
			// Preserve native JSON escaping, nonce generation and queue ownership.
			if (reply == 1 || !length || length >= capacity)
			{
				return length;
			}

			std::string payload{buffer, length};
			constexpr std::string_view old_name = "CLOSE_ACTIVITY_JOIN_REQUEST";

			if (const auto offset = payload.find(old_name); offset != std::string::npos)
			{
				payload.replace(offset, old_name.size(), "CLOSE_ACTIVITY_REQUEST");
				std::memcpy(buffer, payload.c_str(), payload.size() + 1);
				return payload.size();
			}

			return length;
		}

		std::intptr_t initialize_stub(const char*, const discord_handlers* handlers, int, const char*)
		{
			register_settings();

			if (stopping)
			{
				return 0;
			}

			stock_handlers = handlers ? *handlers : discord_handlers{};
			auto adapted = stock_handlers;
			adapted.join_game = accept_invite;
			adapted.spectate_game = nullptr;
			adapted.join_request = game::environment::is_singleplayer() ? nullptr : request_join;

			const auto executable = utils::nt::library::get_by_address(initialize_stub).get_path().wstring();
			const auto command = mode_switch::arguments::quote(executable)
				+ L" -discord-launch -multiplayer -profile "
				+ mode_switch::arguments::quote(utils::string::convert(player_profile::name()));

			if (command.size() >= 1024)
			{
				console::error("[discord] Launch command exceeds the native registration limit.\n");
				return 0;
			}

			register_launch(utils::string::convert(application_id).c_str(), command.c_str());
			const auto result = initialize_hook.invoke<std::intptr_t>(application_id, &adapted, 0, nullptr);
			initialized = true;
			last_signature.clear();

			return result;
		}

		void shutdown_stub()
		{
			if (initialized)
			{
				shutdown_hook.invoke<void>();
			}

			initialized = false;
			join_secret.clear();
			advertised_session.clear();
			advertised_endpoint.clear();
			advertised_token = 0;
			last_signature.clear();
			refresh_at = 0;

			if (game::environment::uses_multiplayer_binary())
			{
				clear_requests();
			}
		}

		void tick()
		{
			if (stopping || !initialized)
			{
				return;
			}

			const auto value = adapt_presence(*native_presence.get());
			const auto signature = join_secret + "|" + std::to_string(value.party_size)
				+ "|" + std::to_string(value.party_max);

			if (signature != last_signature)
			{
				presence_hook.invoke<int>(&value);
				last_signature = signature;
			}
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated())
			{
				return;
			}

			scheduler::once(register_settings, scheduler::main);
			initialize_hook.create(game::select(0x19A780, 0x3A030), initialize_stub);
			presence_hook.create(game::select(0x19ABD0, 0x3A3E0), presence_stub);
			shutdown_hook.create(game::select(0x19AB00, 0x3A310), shutdown_stub);

			if (game::environment::uses_multiplayer_binary())
			{
				// The native menu table uses the generic logo for MP (1E4A80).
				utils::hook::set<const char*>(0xE06070_g, "s2_dil_app_menu_mp");

				respond_hook.create(0x19A8D0_g, respond_stub);
				respond_to_request_hook.create(0x1E2050_g, respond_to_request_stub);
				respond_to_all_requests_hook.create(0x1E1EB0_g, respond_to_all_requests_stub);
				serialize_reply_hook.create(0x19B350_g, serialize_reply_stub);

				scheduler::loop(tick, scheduler::main, 250ms);
			}

			if (const auto invite = utils::flags::get_value("-discord-invite"))
			{
				scheduler::once([secret = *invite]
				{
					accept_invite(secret.c_str());
				}, scheduler::main);
			}
		}

		void pre_destroy() override
		{
			stopping = true;

			if (initialized)
			{
				shutdown_stub();
			}
		}
	};
}

REGISTER_COMPONENT(discord::component)
