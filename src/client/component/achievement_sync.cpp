#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "achievement_sync.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "steam/steam.hpp"

#include <utils/hook.hpp>

namespace achievement_sync
{
	namespace
	{
		std::atomic_bool accepting_refresh_requests{};
		std::atomic_uint64_t local_user{};
		unsigned int refresh_attempts_remaining{};
		bool full_refresh_requested{};
		std::vector<std::string> pending_names;
		utils::hook::detour fetch_by_id_failed_hook;

		std::int64_t fetch_by_id_failed(void* task)
		{
			const auto result = fetch_by_id_failed_hook.invoke<std::int64_t>(task);

			// Task 133's failure callback only resets its request context. Unlike a
			// full fetch failure, it does not invalidate the cache or request a retry.
			request_refresh();
			return result;
		}

		bool fetch_pending_achievements()
		{
			if (!full_refresh_requested)
			{
				// Stock 0x13E540 submits up to ten IDs to 0x13E220. That entry point
				// uses task 133 and a 1,000-byte name list, without clearing the cache.
				std::array<int, 10> ids{};
				std::size_t count{};
				std::size_t length{};
				for (const auto& name : pending_names)
				{
					const auto required = name.size() + 2 + (count ? 1 : 0);
					if (count == ids.size() || length + required >= 1000)
					{
						break;
					}

					const auto id = game::AE_ChallengeNameToId(name.c_str());
					if (id < 0)
					{
						full_refresh_requested = true;
						break;
					}

					ids[count++] = id;
					length += required;
				}

				if (!full_refresh_requested)
				{
					if (!count || !game::AE_FetchAchievementsById(0, ids.data(), static_cast<unsigned int>(count)))
					{
						return false;
					}

					pending_names.erase(pending_names.begin(), pending_names.begin() + count);
					return true;
				}
			}

			std::array<char, 32> transaction{};
			game::AE_GenerateTransactionId(transaction.data());
			if (!game::AE_FetchUserAchievements(0, transaction.data()))
			{
				return false;
			}

			full_refresh_requested = false;
			pending_names.clear();
			return true;
		}

		bool refresh_user_achievements()
		{
			if (!accepting_refresh_requests.load())
			{
				refresh_attempts_remaining = 0;
				full_refresh_requested = false;
				pending_names.clear();
				return scheduler::cond_end;
			}

			if (fetch_pending_achievements())
			{
				refresh_attempts_remaining = pending_names.empty() ? 0 : 31;
				return refresh_attempts_remaining == 0;
			}

			return --refresh_attempts_remaining == 0;
		}

		void queue_refresh(std::vector<std::string> names)
		{
			if (!accepting_refresh_requests.load())
			{
				return;
			}

			scheduler::once([names = std::move(names)]
			{
				if (!accepting_refresh_requests.load())
				{
					return;
				}

				full_refresh_requested |= names.empty();
				if (full_refresh_requested)
				{
					pending_names.clear();
				}
				else
				{
					for (const auto& name : names)
					{
						if (std::ranges::find(pending_names, name) == pending_names.end())
						{
							pending_names.push_back(name);
						}
					}
				}

				const auto already_pending = refresh_attempts_remaining != 0;
				refresh_attempts_remaining = 31;
				if (!already_pending)
				{
					// Both native entry points reject an active fetch or task backoff.
					// Keep the same retry cadence and normal Demonware response pump.
					scheduler::schedule(refresh_user_achievements, scheduler::pipeline::main, 1s);
				}
			}, scheduler::pipeline::main);
		}
	}

	std::uint64_t local_user_id()
	{
		return local_user.load();
	}

	void request_refresh()
	{
		queue_refresh({});
	}

	void request_progress_refresh(std::vector<std::string> names)
	{
		if (!names.empty())
		{
			queue_refresh(std::move(names));
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			accepting_refresh_requests = !game::environment::is_dedicated();
			if (accepting_refresh_requests)
			{
				fetch_by_id_failed_hook.create(0x13D0C0_g, fetch_by_id_failed);
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
