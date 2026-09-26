#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "achievement_sync.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"

namespace achievement_sync
{
	namespace
	{
		std::atomic_bool accepting_refresh_requests{};
		// Only main-thread callbacks access this counter. Zero means no retry is queued.
		unsigned int refresh_attempts_remaining{};

		bool refresh_user_achievements()
		{
			if (!accepting_refresh_requests.load())
			{
				refresh_attempts_remaining = 0;
				return scheduler::cond_end;
			}

			std::array<char, 32> transaction{};
			game::AE_GenerateTransactionId(transaction.data());
			if (game::AE_FetchUserAchievements(0, transaction.data()))
			{
				refresh_attempts_remaining = 0;
				return scheduler::cond_end;
			}

			return --refresh_attempts_remaining == 0;
		}
	}

	void request_refresh()
	{
		if (!accepting_refresh_requests.load())
		{
			return;
		}

		scheduler::once([]
		{
			if (!accepting_refresh_requests.load())
			{
				return;
			}

			const auto already_pending = refresh_attempts_remaining != 0;
			refresh_attempts_remaining = 31;
			if (!already_pending)
			{
				// The stock entry point rejects an active fetch or task backoff.
				// Retry only while a local save is waiting to be fetched. Responses
				// arrive through bdReward and the game's normal Demonware pump.
				scheduler::schedule(refresh_user_achievements, scheduler::pipeline::main, 1s);
			}
		}, scheduler::pipeline::main);
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			accepting_refresh_requests = !game::environment::is_dedicated() &&
				game::environment::is_zombies();
		}

		void pre_destroy() override
		{
			accepting_refresh_requests = false;
		}
	};
}

REGISTER_COMPONENT(achievement_sync::component)
