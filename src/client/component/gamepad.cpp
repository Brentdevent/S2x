#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "dvars.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>

namespace gamepad
{
	namespace
	{
		game::dvar_t* register_allow_aim_assist_stub(const char* name, const bool,
			const game::DvarFlags flags)
		{
			const auto replicated_flags = static_cast<game::DvarFlags>(
				flags | game::DVAR_FLAG_REPLICATED);

			return game::Dvar_RegisterBool(name, true, replicated_flags);
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			// Preserve the engine defaults and ranges, replacing CHEAT with SAVED in both binaries.
			dvars::override::register_float("gpad_stick_deadzone_min", 0.2f, 0.0f, 1.0f, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("gpad_stick_deadzone_max", 0.01f, 0.0f, 1.0f, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("gpad_button_deadzone", 0.13f, 0.0f, 1.0f, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("gpad_stick_pressed", 0.4f, 0.0f, 1.0f, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("gpad_stick_pressed_hysteresis", 0.1f, 0.0f, 1.0f, game::DVAR_FLAG_SAVED);

			if (game::environment::is_multiplayer())
			{
				// Enable sv_allowAimAssist and replicate it to clients.
				utils::hook::call(0x5DE6EC_g, register_allow_aim_assist_stub);
			}
		}
	};
}

REGISTER_COMPONENT(gamepad::component)
