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

#include "steam/interface.hpp"
#include "steam/steam.hpp"

namespace steam_proxy
{
	namespace
	{
		utils::nt::library steam_client_module{};
		utils::nt::library steam_overlay_module{};

		steam::HSteamPipe steam_pipe = 0;
		steam::HSteamUser global_user = 0;

		steam::interface client_engine{};
		steam::interface client_user{};
		steam::interface client_utils{};
		steam::interface client_friends{};

		steam::client* steam_client{};

		enum class ownership_state
		{
			success,
			unowned,
			nosteam,
			error,
		};

		bool is_disabled()
		{
			static const auto disabled = utils::flags::has_flag("nosteam");
			return disabled;
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
			client_engine = nullptr;
			client_user = nullptr;
			client_utils = nullptr;
			client_friends = nullptr;

			steam_pipe = 0;
			global_user = 0;

			steam_client_module = utils::nt::library{nullptr};
		}

		bool perform_cleanup_if_needed()
		{
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
			load_client();
			perform_cleanup_if_needed();
		}

		void post_unpack() override
		{
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
			if (steam_client_module && steam_pipe)
			{
				if (global_user)
				{
					steam_client_module.invoke<void>("Steam_ReleaseUser", steam_pipe,
					                                 global_user);
				}

				steam_client_module.invoke<bool>("Steam_BReleaseSteamPipe", steam_pipe);
			}
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
		if (client_friends)
		{
			return client_friends.invoke<const char*>("GetPersonaName");
		}

		return "S2x";
	}

	void initialize()
	{
		if (client_engine || !steam_client_module) return;

		steam_client = steam_client_module.invoke<steam::client*>("CreateInterface", "SteamClient017", nullptr);
		if (!steam_client) return;

		steam_pipe = steam_client->CreateSteamPipe();
		global_user = steam_client->ConnectToGlobalUser(steam_pipe);
	}
}

REGISTER_COMPONENT(steam_proxy::component)
