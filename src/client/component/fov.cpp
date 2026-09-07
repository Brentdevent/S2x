#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "dvars.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>

namespace fov
{
	namespace
	{
		constexpr auto maximum_fov = 160.0f;

		utils::hook::detour get_fov_range_hook;

		game::dvar_t* register_float(const char* name, const float value, const float min,
			const float max, const game::DvarFlags flags)
		{
			// MP camera readers require the engine's protected float representation.
			return game::environment::is_singleplayer()
				? game::Dvar_RegisterFloat(name, value, min, max, flags)
				: game::Dvar_RegisterFloatProtected(name, value, min, max, flags);
		}

		game::dvar_t* register_fov_stub(const char* name, const float value, const float min,
			const float /*max*/, const game::DvarFlags flags)
		{
			return register_float(name, value, min, maximum_fov, flags);
		}

		bool get_fov_range_stub(int* min, int* max)
		{
			// Shared by the stock domain callback, Settings list, and mode-change adjustment.
			const auto result = get_fov_range_hook.invoke<bool>(min, max);
			*max = static_cast<int>(maximum_fov);
			return result;
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			// Dedicated servers must use the same NETWORK dvar table as clients.
			dvars::override::register_local_float("cg_fovScale", 1.0f, 0.2f,
				game::environment::is_singleplayer() ? 2.0f : 5.0f);

			utils::hook::call(game::select(0x50836, 0x470A82), register_fov_stub);
			utils::hook::call(game::select(0x5085B, 0x470AB1), register_fov_stub);
			get_fov_range_hook.create(game::CG_GetFovRange, get_fov_range_stub);

			if (game::environment::is_singleplayer())
			{
				// SP inlines the upper FOV limit in display adjustment and camera updates.
				// Replace each cmp/cmov pair with mov eax, 160; preserve the calculated minimum.
				for (const auto address : {0x240D5A_g, 0x2426D7_g})
				{
					utils::hook::set<uint8_t>(address, 0xB8);
					utils::hook::set<uint32_t>(address + 1, static_cast<uint32_t>(maximum_fov));
				}
			}
		}
	};
}

REGISTER_COMPONENT(fov::component)
