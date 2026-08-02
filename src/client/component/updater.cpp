#include <std_include.hpp>

#include "updater.hpp"

#include "component/console/console.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

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
		catch (...)
		{
		}
	}

	class component final : public generic_component
	{
	public:
		void post_load() override
		{
			update_thread_ = std::thread(update);
		}

		void post_unpack() override
		{
			join_update_thread();
		}

		void pre_destroy() override
		{
			join_update_thread();
		}

		component_priority priority() const override
		{
			return component_priority::updater;
		}

	private:
		void join_update_thread()
		{
			if (update_thread_.joinable())
			{
				update_thread_.join();
			}
		}

		std::thread update_thread_{};
	};
}

REGISTER_COMPONENT(updater::component)
