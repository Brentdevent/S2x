#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console/console.hpp"

#include "game/game.hpp"
#include "game/pause/process.hpp"

namespace pause::hard
{
	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::is_zombies() || game::environment::is_dedicated())
			{
				return;
			}

			command::add("pause_hard", []
			{
				if (game::virtual_lobby_loaded() || !game::CL_IsLocalClientInGame(0))
				{
					console::info("Hard pause: join a Zombies match first.\n");
					return;
				}

				try
				{
					if (!pause::process::start())
					{
						console::info("Hard pause: a resume dialog is already open or the game is closing.\n");
					}
				}
				catch (const std::exception& error)
				{
					console::error("Hard pause: could not open the resume dialog. %s\n", error.what());
				}
			});
		}

		void pre_destroy() override
		{
			pause::process::stop();
		}
	};
}

REGISTER_COMPONENT(pause::hard::component)
