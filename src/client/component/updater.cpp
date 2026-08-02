#include <std_include.hpp>

#include "updater.hpp"

#include "component/console/console.hpp"
#include "game/game.hpp"

#include <updater/updater.hpp>

#include <utils/flags.hpp>
#include <utils/nt.hpp>

namespace updater
{
	void update()
	{
		try
		{
			cleanup();

			if (utils::flags::has_flag("-noupdate"))
			{
				return;
			}

			run(game::get_appdata_path());
		}
		catch (const update_cancelled&)
		{
			utils::nt::terminate(0);
		}
		catch (const std::exception& e)
		{
			console::error("[Updater] %s\n", e.what());
		}
		catch (...)
		{
			console::error("[Updater] Update failed with an unknown error.\n");
		}
	}
}
