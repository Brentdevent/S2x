#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "achievement_sync.hpp"
#include "component/scheduler.hpp"

#include "game/game.hpp"
#include "game/demonware/achievement_response.hpp"
#include "game/demonware/achievement_store.hpp"

#include <utils/hook.hpp>

#include <mutex>
#include <optional>

namespace achievement_sync
{
	namespace
	{
		// Verified native AE user-achievement task layout.
		constexpr std::ptrdiff_t achievement_task_active_offset = 0xC0; // Active flag.
		constexpr std::ptrdiff_t achievement_transaction_offset = 0xD0;
		constexpr std::ptrdiff_t achievement_response_offset = 0xF8;
		constexpr std::size_t achievement_transaction_size = 25; // Transaction buffer.

		utils::hook::detour fetch_user_achievements_hook;
		std::atomic_bool accepting_refresh_requests{};
		std::atomic_bool persisted_achievements_available{};
		std::atomic_uint64_t achievement_refresh_generation{};
		std::mutex pending_achievement_update_mutex{};
		std::optional<std::string> pending_achievement_update{};
		bool virtual_lobby_was_loaded{};

		void clear_pending_achievement_update()
		{
			std::lock_guard lock{pending_achievement_update_mutex};
			pending_achievement_update.reset();
		}

		void queue_achievement_update(std::string transaction)
		{
			std::lock_guard lock{pending_achievement_update_mutex};
			pending_achievement_update = std::move(transaction);
		}

		std::optional<std::string> take_achievement_update()
		{
			std::lock_guard lock{pending_achievement_update_mutex};
			auto transaction = std::move(pending_achievement_update);
			pending_achievement_update.reset();
			return transaction;
		}

		bool fetch_user_achievements_stub(const unsigned int controller_index, const char* page_token,
			const void* transaction_id, const unsigned int account_index)
		{
			const auto result = fetch_user_achievements_hook.invoke<bool>(controller_index,
				page_token, transaction_id, account_index);
			if (result && transaction_id && controller_index == 0 &&
				persisted_achievements_available.load())
			{
				queue_achievement_update(static_cast<const char*>(transaction_id));
			}

			return result;
		}

		void dispatch_user_achievement_updates()
		{
			if (!persisted_achievements_available.load())
			{
				clear_pending_achievement_update();
				return;
			}

			const auto transaction = take_achievement_update();
			if (!transaction)
			{
				return;
			}

			const auto response = demonware::achievement_response::
				make_get_user_achievements_response(*transaction);
			auto* response_object = game::AE_UserAchievementTaskData.get() +
				achievement_response_offset;
			if (game::AE_SetResponseString(response_object, response.c_str()))
			{
				game::AE_ProcessResponse(0, response_object, 0);
			}
		}

		bool is_user_achievement_fetch_active()
		{
			const auto* active = reinterpret_cast<const std::uint32_t*>(
				game::AE_UserAchievementTaskData.get() + achievement_task_active_offset);
			return *active != 0;
		}

		bool queue_active_achievement_update()
		{
			const auto* transaction = reinterpret_cast<const char*>(
				game::AE_UserAchievementTaskData.get() + achievement_transaction_offset);
			const auto* end = std::find(transaction,
				transaction + achievement_transaction_size, '\0');
			if (end == transaction || end == transaction + achievement_transaction_size)
			{
				return false;
			}

			queue_achievement_update({transaction, end});
			return true;
		}

		bool start_user_achievement_refresh()
		{
			if (is_user_achievement_fetch_active())
			{
				return queue_active_achievement_update();
			}

			std::array<char, 32> transaction_id{};
			game::AE_GenerateTransactionId(transaction_id.data());

			// This is the stock in-menu page path and immediately gives the response
			// bridge a transaction. The top-level path can be delayed by the online
			// task scheduler's retry backoff.
			if (game::AE_FetchUserAchievementsByPage(0, "", transaction_id.data(), 0))
			{
				return true;
			}

			return is_user_achievement_fetch_active() && queue_active_achievement_update();
		}

		void refresh_user_achievements_when_ready()
		{
			if (!persisted_achievements_available.load())
			{
				return;
			}

			const auto generation = ++achievement_refresh_generation;
			if (start_user_achievement_refresh())
			{
				return;
			}

			scheduler::schedule([generation, attempts = 0]() mutable
			{
				if (!persisted_achievements_available.load() ||
					achievement_refresh_generation.load() != generation)
				{
					return scheduler::cond_end;
				}

				if (start_user_achievement_refresh())
				{
					return scheduler::cond_end;
				}

				if (++attempts >= 30)
				{
					return scheduler::cond_end;
				}

				return scheduler::cond_continue;
			}, scheduler::pipeline::main, 1s);
		}

		void refresh_user_achievements_on_lobby_entry()
		{
			const auto virtual_lobby_loaded = game::virtual_lobby_loaded();
			if (!virtual_lobby_loaded)
			{
				virtual_lobby_was_loaded = false;
				return;
			}

			if (virtual_lobby_was_loaded)
			{
				return;
			}

			virtual_lobby_was_loaded = true;
			if (!persisted_achievements_available.load())
			{
				persisted_achievements_available =
					!demonware::achievement_store::get_all().empty();
			}

			refresh_user_achievements_when_ready();
		}
	}

	void request_refresh()
	{
		if (!accepting_refresh_requests.load())
		{
			return;
		}

		persisted_achievements_available = true;
		scheduler::once([]
		{
			if (accepting_refresh_requests.load())
			{
				refresh_user_achievements_when_ready();
			}
		}, scheduler::pipeline::main);
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated() || !game::environment::is_zombies())
			{
				return;
			}

			accepting_refresh_requests = true;
			persisted_achievements_available =
				!demonware::achievement_store::get_all().empty();
			virtual_lobby_was_loaded = false;
			fetch_user_achievements_hook.create(game::AE_FetchUserAchievementsByPage,
				fetch_user_achievements_stub);
			scheduler::loop(refresh_user_achievements_on_lobby_entry,
				scheduler::pipeline::main, 100ms);
			scheduler::loop(dispatch_user_achievement_updates,
				scheduler::pipeline::main, 50ms);
		}

		void pre_destroy() override
		{
			accepting_refresh_requests = false;
			persisted_achievements_available = false;
			++achievement_refresh_generation;
			clear_pending_achievement_update();
		}
	};
}

REGISTER_COMPONENT(achievement_sync::component)
