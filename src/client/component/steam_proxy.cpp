#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "steam_proxy.hpp"
#include "scheduler.hpp"

#include <utils/nt.hpp>
#include <utils/flags.hpp>
#include <utils/string.hpp>
#include <utils/finally.hpp>
#include <utils/concurrency.hpp>
#include <utils/binary_resource.hpp>

#include "resource.hpp"

#include "game/game.hpp"

#include "steam/steam.hpp"
#include "steam/interface.hpp"

namespace steam_proxy
{
	namespace
	{
		std::recursive_mutex client_mutex;
		utils::nt::library steam_client_module{};
		utils::nt::library steam_overlay_module{};

		steam::HSteamPipe steam_pipe = 0;
		steam::HSteamUser global_user = 0;

		steam::interface client_engine{};
		steam::interface client_user{};
		steam::interface client_utils{};
		steam::interface client_friends{};

		steam::friends* steam_friends{};
		steam::utils* steam_utils{};
		bool callbacks_active = false;
		DWORD callback_thread{};
		std::unordered_set<uint64_t> known_friends;

		// CallbackMsg_t uses 32-bit handles and 8-byte packing on Windows x64.
		struct callback_message
		{
			int32_t user;
			int type;
			void* data;
			int size;
		};

		static_assert(sizeof(callback_message) == 24);
		static_assert(offsetof(callback_message, type) == 4);
		static_assert(offsetof(callback_message, data) == 8);
		static_assert(offsetof(callback_message, size) == 16);

		struct friend_callback
		{
			int type;
			int size;
			alignas(8) std::array<uint8_t, sizeof(steam::avatar_image_loaded)> data{};
		};

		int friend_callback_size(const int type)
		{
			switch (type)
			{
			case steam::persona_state_change::callback_id:
				return sizeof(steam::persona_state_change);
			case steam::avatar_image_loaded::callback_id:
				return sizeof(steam::avatar_image_loaded);
			case steam::friend_rich_presence_update::callback_id:
				return sizeof(steam::friend_rich_presence_update);
			default:
				return 0;
			}
		}

		std::vector<friend_callback> poll_friend_callbacks()
		{
			std::lock_guard _{client_mutex};
			std::vector<friend_callback> events;

			if (!callbacks_active)
			{
				return events;
			}

			if (!steam_friends)
			{
				// Remove entries already cached by the native friends menu after Steam exits.
				for (const auto id : known_friends)
				{
					const steam::persona_state_change change{id, 0x208}; // Offline + relationship changed.
					friend_callback event{steam::persona_state_change::callback_id, sizeof(change)};
					std::memcpy(event.data.data(), &change, sizeof(change));
					events.push_back(event);
				}

				known_friends.clear();
				return events;
			}

			const auto get_callback = steam_client_module.get_proc<bool(*)(int32_t, callback_message*, int32_t*)>(
				"Steam_BGetCallback");
			const auto free_callback = steam_client_module.get_proc<void(*)(int32_t)>("Steam_FreeLastCallback");

			if (!get_callback || !free_callback)
			{
				return events;
			}

			callback_message message{};
			int32_t ignored{};

			for (auto i = 0; i < 256 && get_callback(steam_pipe, &message, &ignored); ++i)
			{
				const auto release = utils::finally([&] { free_callback(steam_pipe); });
				const auto size = friend_callback_size(message.type);

				// Retail lobby/authentication callbacks must not reach S2x's emulation.
				if (!size || message.user != global_user || !message.data || message.size != size)
				{
					continue;
				}

				friend_callback event{message.type, size};
				std::memcpy(event.data.data(), message.data, size);
				events.push_back(event);
			}

			return events;
		}

		enum class ownership_state
		{
			success,
			unowned,
			nosteam,
			error,
		};

		bool is_disabled()
		{
			return utils::flags::has_flag("-nosteam") ||
				   utils::nt::is_wine() ||
			       game::environment::is_microsoft_store() ||
			       game::environment::is_dedicated();
		}

		void* load_client_engine()
		{
			if (!steam_client_module) return nullptr;

			for (auto i = 1; i <= 999; ++i)
			{
				std::string name = utils::string::va("CLIENTENGINE_INTERFACE_VERSION%03i", i);
				auto* const temp_client_engine = steam_client_module
					.invoke<void*>("CreateInterface", name.data(), nullptr);
				if (temp_client_engine) 
					return temp_client_engine;
			}

			return nullptr;
		}

		void load_client()
		{
			std::lock_guard _{client_mutex};

			SetEnvironmentVariableA("SteamAppId", std::to_string(steam::SteamUtils()->GetAppID()).data());

			const std::filesystem::path steam_path = steam::SteamAPI_GetSteamInstallPath();
			if (steam_path.empty()) return;

			utils::nt::library::load(steam_path / "tier0_s64.dll");
			utils::nt::library::load(steam_path / "vstdlib_s64.dll");
			steam_overlay_module = utils::nt::library::load(steam_path / "gameoverlayrenderer64.dll");
			steam_client_module = utils::nt::library::load(steam_path / "steamclient64.dll");
			if (!steam_client_module) return;

			client_engine = load_client_engine();
			if (!client_engine) return;

			steam_pipe = steam_client_module.invoke<steam::HSteamPipe>("Steam_CreateSteamPipe");
			global_user = steam_client_module.invoke<steam::HSteamUser>(
				"Steam_ConnectToGlobalUser", steam_pipe);

			client_user = client_engine.invoke<void*>(8, steam_pipe, global_user);
			client_utils = client_engine.invoke<void*>(14, steam_pipe);
			client_friends = client_engine.invoke<void*>(13, global_user, steam_pipe);
		}

		void do_cleanup()
		{
			std::lock_guard _{client_mutex};
			steam_friends = nullptr;
			steam_utils = nullptr;

			client_engine = nullptr;
			client_user = nullptr;
			client_utils = nullptr;
			client_friends = nullptr;

			if (steam_client_module && steam_pipe)
			{
				if (global_user)
				{
					steam_client_module.invoke<void>("Steam_ReleaseUser", steam_pipe, global_user);
				}

				steam_client_module.invoke<bool>("Steam_BReleaseSteamPipe", steam_pipe);
			}

			steam_pipe = 0;
			global_user = 0;

			steam_client_module = utils::nt::library{nullptr};
		}

		bool perform_cleanup_if_needed()
		{
			std::lock_guard _{client_mutex};

			if (steam_client_module
				&& steam_pipe
				&& global_user
				&& steam_client_module.invoke<bool>("Steam_BConnected", global_user,
				                                    steam_pipe)
				&& steam_client_module.invoke<bool>("Steam_BLoggedOn", global_user, steam_pipe)
			)
			{
				return false;
			}

			do_cleanup();
			return true;
		}

		void clean_up_on_error()
		{
			scheduler::schedule([]
			{
				if (perform_cleanup_if_needed())
				{
					return scheduler::cond_end;
				}

				return scheduler::cond_continue;
			}, scheduler::main);
		}

		ownership_state start_mod_unsafe(const std::string& title, size_t app_id)
		{
			std::lock_guard _{client_mutex};

			if (!client_utils || !client_user)
			{
				return ownership_state::nosteam;
			}

			if (!client_user.invoke<bool>("BIsSubscribedApp", app_id))
			{
				//app_id = 480; // Spacewar
				return ownership_state::unowned;
			}

			if (is_disabled())
			{
				return ownership_state::success;
			}

			client_utils.invoke<void>("SetAppIDForCurrentPipe", app_id, false);

			return ownership_state::success;
		}

		ownership_state start_mod(const std::string& title, const size_t app_id)
		{
			__try
			{
				return start_mod_unsafe(title, app_id);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				do_cleanup();
				return ownership_state::error;
			}
		}
	}

	class component final : public generic_component
	{
	public:
		void post_load() override
		{
			callback_thread = GetCurrentThreadId();
			if (is_disabled()) return;

			load_client();
			perform_cleanup_if_needed();
		}

		void post_unpack() override
		{
			if (is_disabled()) return;

			try
			{
				const auto res = start_mod("\xE2\xAD\x90" " S2x"s, steam::SteamUtils()->GetAppID());

				switch (res)
				{
				case ownership_state::nosteam:
					throw std::runtime_error("Steam must be running to play this game!");
				case ownership_state::unowned:
					throw std::runtime_error("You must own the game on steam to play this mod!");
				case ownership_state::error:
					throw std::runtime_error("Failed to verify ownership of the game!");
				case ownership_state::success:
					break;
				}
			}
			catch (std::exception& e)
			{
				printf("Steam: %s\n", e.what());
				MessageBoxA(GetForegroundWindow(), e.what(), "S2x Error", MB_ICONERROR);
				TerminateProcess(GetCurrentProcess(), 1234);
			}

			clean_up_on_error();
		}

		void pre_destroy() override
		{
			shutdown();
			do_cleanup();
		}

		component_priority priority() const override
		{
			return component_priority::steam_proxy;
		}
	};

	const utils::nt::library& get_overlay_module()
	{
		return steam_overlay_module;
	}

	const char* get_player_name()
	{
		std::lock_guard _{client_mutex};

		if (client_friends)
		{
			const auto* player_name = client_friends.invoke<const char*>("GetPersonaName");
			if (player_name && *player_name)
			{
				static thread_local std::string name;
				name = player_name;
				return name.c_str();
			}
		}

		static const auto user_name = utils::nt::get_user_name();
		return user_name.empty() ? "Unknown Soldier" : user_name.data();
	}

	void initialize()
	{
		std::lock_guard _{client_mutex};

		if (is_disabled() || !steam_client_module || !steam_pipe || !global_user)
		{
			return;
		}

		auto* steam_client = steam_client_module.invoke<steam::client*>("CreateInterface", "SteamClient017", nullptr);

		if (!steam_client)
		{
			return;
		}

		steam_friends = static_cast<steam::friends*>(steam_client->GetISteamFriends(global_user, steam_pipe, "SteamFriends015"));
		steam_utils = static_cast<steam::utils*>(steam_client->GetISteamUtils(steam_pipe, "SteamUtils009"));
		callbacks_active = true;
	}

	client_access access_client()
	{
		return {std::unique_lock{client_mutex}, steam_friends, steam_utils};
	}

	void remember_friend(const uint64_t id)
	{
		std::lock_guard _{client_mutex};

		if (id)
		{
			known_friends.insert(id);
		}
	}

	void run_callbacks()
	{
		// The authentication worker can also pump Steam. Leave real events queued
		// until the game's main thread calls us, before invoking any engine code.
		if (GetCurrentThreadId() != callback_thread)
		{
			return;
		}

		// Release the client lock before calling game code, which may query Steam again.
		auto events = poll_friend_callbacks();

		for (auto& event : events)
		{
			steam::callbacks::dispatch_callback(event.data.data(), event.size, event.type);
		}
	}

	void shutdown()
	{
		std::lock_guard _{client_mutex};
		callbacks_active = false;
		steam_friends = nullptr;
		steam_utils = nullptr;
		known_friends.clear();
	}
}

REGISTER_COMPONENT(steam_proxy::component)
