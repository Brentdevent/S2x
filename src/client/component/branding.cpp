#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"
#include "version.hpp"

#include "scheduler.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

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

			game::R_AddCmdDrawText("S2x: " VERSION, 0x7fffffff, font, 0, 0, font->pixelHeight, x, y, 1.0f, 1.0f, 0.0f, color, nullptr);
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

			scheduler::once([]
			{
				static const auto binary = game::environment::get_binary_string();
				game::Dvar_RegisterString("s2x_runtime", binary.data(), game::DVAR_FLAG_WRITE);
			}, scheduler::main);

			// Change window title prefix
			if (game::environment::uses_multiplayer_binary())
			{
				const auto* platform = game::environment::is_store_native() ? "MS" : "Steam";
				const auto* mode = game::environment::is_zombies() ? "Zombies" : "Multiplayer";
				utils::hook::copy_string(game::select(0xBA6040, 0xBAC630), utils::string::va("S2x (%s) - %s", platform, mode));
			}
			else
			{
				utils::hook::call(game::select(0, 0, 0x511738), multi_byte_to_wide_char_stub);
				utils::hook::nop(game::select(0, 0, 0x511738) + 5, 1);
			}
		}
	};
}

REGISTER_COMPONENT(branding::component)
