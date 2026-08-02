#include <std_include.hpp>

#include "updater.hpp"
#include "file_updater.hpp"
#include "updater_ui.hpp"

#include <game/game.hpp>

#include <utils/nt.hpp>
#include <utils/io.hpp>

namespace updater
{
	void cleanup()
	{
		const auto self = utils::nt::library::get_by_address(cleanup);
		auto old_self_file = self.get_path();
		old_self_file += ".old";

		for (auto attempt = 0; attempt < 4; ++attempt)
		{
			if (utils::io::remove_file(old_self_file) && !utils::io::file_exists(old_self_file.wstring()))
			{
				return;
			}

			if (attempt < 3)
			{
				std::this_thread::sleep_for(2s);
			}
		}

		throw std::runtime_error("Unable to remove s2x.exe.old.");
	}

	void run(const std::filesystem::path& base)
	{
		const auto self = utils::nt::library::get_by_address(run);
		const auto self_file = self.get_path();

		updater_ui updater_ui{game::environment::is_dedi()};
		const file_updater file_updater{updater_ui, base, self_file};
		file_updater.run();
	}
}
