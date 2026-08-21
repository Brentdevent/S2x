#include <std_include.hpp>

#include <version.hpp>

#include "updater.hpp"

#include "component/console/console.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

#include <updater/updater.hpp>

#include <utils/cryptography.hpp>
#include <utils/flags.hpp>
#include <utils/io.hpp>
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

		bool running_binary_was_replaced()
		{
			const auto self = utils::nt::library::get_by_address(running_binary_was_replaced);
			std::string installed_binary{};
			if (!utils::io::read_file(self.get_path().wstring(), &installed_binary))
			{
				return false;
			}

			return installed_binary.find(GIT_HASH) == std::string::npos;
		}

		[[noreturn]] void relaunch_installed_binary()
		{
			if (!utils::nt::relaunch_self())
			{
				console::error("[Updater] The installed executable changed, but S2x could not be relaunched.\n");
				utils::nt::terminate(1);
			}

			utils::nt::terminate(0);
		}

		void cleanup_previous_binary()
		{
			try
			{
				cleanup();
			}
			catch (const std::exception& e)
			{
				console::warn("[Updater] Deferred executable cleanup: %s\n", e.what());
			}
		}

		void perform_update()
		{
			try
			{
				run(game::get_appdata_path());
			}
			catch (const update_cancelled&)
			{
				utils::nt::terminate(0);
			}
			catch (const std::exception& e)
			{
				console::error("[Updater] Update failed: %s\n", e.what());
			}
		}
	}

	void update()
	{
		try
		{
			const utils::named_mutex update_mutex{get_update_mutex_name()};
			const std::unique_lock update_lock{update_mutex};
			const auto binary_was_replaced = running_binary_was_replaced();

			cleanup_previous_binary();

			if (!utils::flags::has_flag("-noupdate"))
			{
				perform_update();
			}

			if (binary_was_replaced)
			{
				relaunch_installed_binary();
			}
		}
		catch (...)
		{
		}
	}

	void update_store_runtime(const std::filesystem::path& base, const std::string& required_file)
	{
		const utils::named_mutex update_mutex{get_update_mutex_name()};
		const std::unique_lock update_lock{update_mutex};

		try
		{
			run_store_runtime_update(base, required_file);
		}
		catch (const std::exception& e)
		{
			throw std::runtime_error("Unable to update '" + required_file + "': " + e.what());
		}
	}

	class component final : public generic_component
	{
	public:
		component()
		{
			update_thread_ = std::thread(update);
		}

		void post_load() override
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
		std::thread update_thread_{};

		void join_update_thread()
		{
			if (update_thread_.joinable())
			{
				update_thread_.join();
			}
		}
	};
}

REGISTER_COMPONENT(updater::component)
