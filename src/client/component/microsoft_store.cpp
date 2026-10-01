#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "callstack.hpp"
#include "console/console.hpp"

#include <utils/hook.hpp>

namespace microsoft_store
{
	namespace
	{
		struct xstore_game_license
		{
			char sku_store_id[18];
			bool is_active;
			bool is_trial_owned_by_this_user;
			bool is_disc_license;
			bool is_trial;
			std::uint32_t trial_time_remaining_in_seconds;
			char trial_unique_id[64];
			std::int64_t expiration_date;
		};

		static_assert(offsetof(xstore_game_license, is_active) == 0x12);
		static_assert(offsetof(xstore_game_license, is_trial) == 0x15);

		HMODULE get_game_module(LPCSTR)
		{
			return reinterpret_cast<HMODULE>(game::get_base());
		}

		HRESULT query_game_license_result_stub(void* async, xstore_game_license* license)
		{
			const auto result = utils::hook::invoke<HRESULT>(0x14088ECEC_ms, async, license);
			if (FAILED(result))
			{
				console::warn("[Store] XStoreQueryGameLicenseResult failed (0x%08X)\n", static_cast<unsigned>(result));
			}

			license->is_active = true;
			license->is_trial = false;
			return S_OK;
		}

		void bd_log_stub(const char* status)
		{
			if (status && !std::strcmp(status, "BD_FAILED"))
			{
				console::warn("[BD] BD_FAILED call stack:\n%s", callstack::capture().data());
			}

			utils::hook::invoke<void>(0x140AA6420_ms, status);
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_load() override
		{
			if (!game::environment::is_store_native())
			{
				return;
			}

			for (const auto address : {0x140714EA8_ms, 0x140714F3E_ms})
			{
				utils::hook::nop(address, 6);
				utils::hook::call(address, get_game_module);
			}

			utils::hook::call(0x14017D362_ms, query_game_license_result_stub);
			utils::hook::set<std::uint32_t>(0x1406F19D0_ms, 0xC301B0);
			utils::hook::call(0x140A81FCB_ms, bd_log_stub);
		}
	};
}

REGISTER_COMPONENT(microsoft_store::component)
