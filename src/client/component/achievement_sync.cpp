#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "achievement_sync.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "steam/steam.hpp"

namespace achievement_sync
{
	namespace
	{
		std::atomic_bool accepting_refresh_requests{};
		std::atomic_uint64_t local_user{};
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

	std::uint64_t local_user_id()
	{
		return local_user.load();
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
			accepting_refresh_requests = !game::environment::is_dedicated();
			if (accepting_refresh_requests)
			{
				scheduler::once([] { local_user = steam::SteamUser()->GetSteamID().bits; }, scheduler::pipeline::main);
			}
		}

		void pre_destroy() override
		{
			accepting_refresh_requests = false;
			local_user = 0;
		}
	};
}

REGISTER_COMPONENT(achievement_sync::component)
