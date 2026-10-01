#include <std_include.hpp>
#include "hq_rewards.hpp"
#include "achievement_claim.hpp"

namespace demonware::hq_rewards
{
	namespace
	{
		achievement_record automatic(const std::string& name, const std::uint32_t item, const std::uint32_t quantity)
		{
			achievement_record record;
			record.name = name;
			record.kind = 5;
			record.fulfilled_times = 0;
			record.status = achievement_status::in_progress;
			if (!item)
			{
				record.success_rewards = "[{\"type\":\"grant_currency\",\"currency\":{\"id\":6,\"amount\":" + std::to_string(quantity) + "}}]";
			}
			else
			{
				record.success_rewards = "[{\"type\":\"grant_product\",\"product\":{\"id\":" + std::to_string(item) +
					",\"items\":[{\"id\":" + std::to_string(item) + ",\"quantity\":" + std::to_string(quantity) +
					",\"usage_duration\":null,\"override_usage_duration\":0}],\"currencies\":[]}}]";
			}
			return record;
		}

		std::optional<std::uint64_t> selector(const reward_game_events::event& event, const char* key)
		{
			std::optional<std::uint64_t> value;
			for (const auto& param : event.parameters)
			{
				if (param.selector == key)
				{
					if (value)
					{
						return {};
					}
					value = param.value;
				}
			}
			return value;
		}
	}

	achievement_record payroll(const bool master_prestige)
	{
		// Retail payroll_officer descriptor: 200 AC. The stock kiosk's master
		// prestige fallback is 300 AC; both variants use its four-hour cooldown.
		return automatic(master_prestige ? "payroll_officer_masterprestige" : "payroll_officer",
			0, master_prestige ? 300 : 200);
	}

	std::vector<achievement_record> initial_records()
	{
		std::vector<achievement_record> records{payroll(false), payroll(true)};
		const auto catalog = loot_catalog::get_snapshot();
		if (catalog)
		{
			for (std::size_t rank = 0; rank < catalog->social_ranks.size(); ++rank)
			{
				const auto& reward = catalog->social_ranks[rank];
				records.push_back(automatic("hit_social_rank_" + std::to_string(rank + 1), reward.item_id, reward.quantity));
			}
		}
		return records;
	}

	bool process(const reward_game_events::event& event, const std::uint64_t user,
		const std::uint32_t timestamp, const loot_catalog::catalog* catalog, std::string& push)
	{
		push.clear();
		if (event.name != "social_score" && event.name != "picked_up_payroll")
		{
			return true;
		}
		if (!user || !timestamp)
		{
			return false;
		}
		const auto value = selector(event, "1");
		if (!value)
		{
			return true;
		}
		const auto is_payroll = event.name == "picked_up_payroll";
		achievement_record record;
		std::uint32_t threshold{};
		if (is_payroll)
		{
			const auto master = selector(event, "2");
			if (*value != 1 || !master || *master > 1)
			{
				return true;
			}
			record = payroll(*master != 0);
		}
		else
		{
			if (!catalog || catalog->social_ranks.empty())
			{
				return false;
			}
			if (!*value || *value > catalog->social_ranks.size())
			{
				return true;
			}
			const auto& reward = catalog->social_ranks[*value - 1];
			threshold = reward.threshold;
			record = automatic("hit_social_rank_" + std::to_string(*value), reward.item_id, reward.quantity);
		}
		if (event.timestamp <= 0)
		{
			return true;
		}
		// bdRewardGameEvent carries epoch milliseconds; accept whole-second SDK
		// callers too. Time never identifies an occurrence: completion state does.
		const auto event_time = static_cast<std::uint64_t>(event.timestamp > 1000000000000LL ?
			event.timestamp / 1000 : event.timestamp);
		const auto saved = achievement_store::complete_hq_reward(record, timestamp, event_time, is_payroll,
			[&](marketplace_store::transaction& economy, const achievement_record& completed, const bool grant)
			{
				if (!is_payroll && economy.get_currency(7) < threshold)
				{
					return false;
				}
				return achievement_claim::settle_reward(economy, completed, grant, user, timestamp, push);
			});
		if (!saved)
		{
			push.clear();
		}
		return saved;
	}
}
