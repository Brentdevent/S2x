#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>

namespace muzzle_flash
{
	namespace
	{
		const game::dvar_t* cg_muzzle_flash{};
		
		utils::hook::detour select_muzzle_flash_hook;

		void* select_muzzle_flash_stub(const void* weapon, const std::uint8_t alternate,
			const int variant, const int view_model, const int flags, const std::uint8_t ads,
			void** secondary_effect)
		{
			if (cg_muzzle_flash && !cg_muzzle_flash->current.enabled)
			{
				if (secondary_effect)
				{
					*secondary_effect = nullptr;
				}

				return nullptr;
			}

			return select_muzzle_flash_hook.invoke<void*>(weapon, alternate, variant,
				view_model, flags, ads, secondary_effect);
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

			cg_muzzle_flash = game::Dvar_RegisterBool("cg_muzzleFlash", true, game::DVAR_FLAG_SAVED);

			select_muzzle_flash_hook.create(game::select(0x3BB400, 0x57BF50), select_muzzle_flash_stub);
		}
	};
}

REGISTER_COMPONENT(muzzle_flash::component)
