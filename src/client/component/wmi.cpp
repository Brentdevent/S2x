#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>

namespace wmi
{
	namespace
	{
		decltype(&CoInitializeEx) co_initialize_ex_original{};

		HRESULT WINAPI co_initialize_ex_stub(LPVOID reserved, const DWORD co_init)
		{
			// Fail COM init for the game's WMI context so it never connects or queries.
			// Its queries then just return E_FAIL. Saves several seconds at startup.
			if (reinterpret_cast<size_t>(_ReturnAddress()) == game::select(0xAB0915, 0x7E94A5))
			{
				return E_FAIL;
			}

			return co_initialize_ex_original(reserved, co_init);
		}
	}

	struct component final : generic_component
	{
	public:
		void post_load() override
		{
			// Must run before the game's static initializers.
			auto* const entry = utils::nt::library{}.get_iat_entry("ole32.dll", "CoInitializeEx");
			if (!entry || !*entry)
			{
				return;
			}

			co_initialize_ex_original = static_cast<decltype(co_initialize_ex_original)>(*entry);
			utils::hook::set(entry, co_initialize_ex_stub);
		}
	};
}

REGISTER_COMPONENT(wmi::component)
