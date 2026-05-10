#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"
#include "version.hpp"

#include "scheduler.hpp"

#include <utils/hook.hpp>

namespace branding
{
	namespace
	{
		void draw_branding()
		{
			constexpr auto x = 5;
			constexpr auto y = 20;
			float color[4] = {0.666f, 0.666f, 0.666f, 0.666f};

			const auto* font = game::R_RegisterFont("fonts/fira_mono_regular.ttf", 16);
			if (!font) return;

			game::R_AddCmdDrawText("S2x: " VERSION, 0x7fffffff, font, 0, 0, font->pixelHeight, x, y, 1.0f, 1.0f, 0.0f, color, 0);
		}

		int multi_byte_to_wide_char_stub(UINT CodePage, DWORD dwFlags, LPCCH lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar)
		{
			return MultiByteToWideChar(CodePage, dwFlags, "S2x - Singleplayer", cbMultiByte, lpWideCharStr, cchWideChar);
		}
	}

	struct component final : generic_component
	{
		void post_unpack() override
		{
			scheduler::loop(draw_branding, scheduler::renderer);

			// Change window title prefix
			if (game::environment::is_mp())
			{
				if (game::environment::is_zombies())
				{
					utils::hook::copy_string(0xBA6040_g, "S2x - Zombies");
				}
				else
				{
					utils::hook::copy_string(0xBA6040_g, "S2x - Multiplayer");
				}
			}
			else
			{
				utils::hook::call(0x511738_g, multi_byte_to_wide_char_stub);
				utils::hook::nop(0x511738_g + 5, 1);
			}
		}
	};
}

REGISTER_COMPONENT(branding::component)
