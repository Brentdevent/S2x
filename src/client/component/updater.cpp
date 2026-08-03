#include <std_include.hpp>

#include "updater.hpp"

#include "component/console/console.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

#include <updater/updater.hpp>

#include <utils/cryptography.hpp>
#include <utils/flags.hpp>
#include <utils/named_mutex.hpp>
#include <utils/nt.hpp>

namespace updater
{
	namespace
	{
		std::string get_update_mutex_name()
		{
			const auto update_root = game::get_appdata_path();

			std::error_code error{};
			auto normalized_path = std::filesystem::weakly_canonical(update_root, error);
			if (error)
			{
				normalized_path = update_root.lexically_normal();
			}

			auto normalized_path_string = normalized_path.native();
			if (!normalized_path_string.empty())
			{
				CharLowerBuffW(normalized_path_string.data(),
					static_cast<DWORD>(normalized_path_string.size()));
			}

			const std::string normalized_path_bytes{
				reinterpret_cast<const char*>(normalized_path_string.data()),
				normalized_path_string.size() * sizeof(wchar_t)
			};

			return "Global\\s2x-updater-" +
				utils::cryptography::sha1::compute(normalized_path_bytes, true);
		}
	}

	void update()
	{
		try
		{
			const utils::named_mutex update_mutex{get_update_mutex_name()};
			const std::unique_lock update_lock{update_mutex};

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
