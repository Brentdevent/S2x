#include <std_include.hpp>
#include "hq_rewards.hpp"
#include "game/demonware/achievement/claim.hpp"

#include <utils/string.hpp>

namespace demonware::hq_rewards
{
	namespace
	{
		constexpr std::uint8_t social_score_currency = 7;
		constexpr std::int64_t millisecond_timestamp = 1000000000000;

		achievement_record automatic(const std::string& name, const std::uint32_t item, const std::uint32_t quantity)
		{
			achievement_record record{};
			record.name = name;
			record.kind = 5;
			record.fulfilled_times = 0;
			record.status = achievement_status::in_progress;

			if (item)
			{
				record.success_rewards = utils::string::va(R"([{"type":"grant_product","product":{"id":%u,"items":[{"id":%u,"quantity":%u,"usage_duration":null,"override_usage_duration":0}],"currencies":[]}}])",
					item, item, quantity);
			}
			else
			{
				record.success_rewards = utils::string::va(R"([{"type":"grant_currency","currency":{"id":6,"amount":%u}}])", quantity);
			}

			return record;
		}

		achievement_record social_rank(const std::uint64_t rank, const loot_catalog::social_rank_reward& reward)
		{
			return automatic("hit_social_rank_" + std::to_string(rank), reward.item_id, reward.quantity);
		}

		std::optional<std::uint64_t> selector(const reward_game_events::event& event, const std::string_view key)
		{
			std::optional<std::uint64_t> value{};

			for (const auto& param : event.parameters)
			{
				if (param.selector != key)
				{
					continue;
				}

				if (value)
				{
					return std::nullopt;
				}

				value = param.value;
			}

			return value;
		}

		// bdRewardGameEvent carries epoch milliseconds, but whole-second SDK callers are accepted too
		std::uint64_t event_seconds(const std::int64_t timestamp)
		{
			return static_cast<std::uint64_t>(timestamp > millisecond_timestamp ? timestamp / 1000 : timestamp);
		}
	}

	// The retail descriptor pays 200 AC, the stock kiosk's master prestige fallback pays 300 AC
	achievement_record payroll(const bool master_prestige)
	{
		const auto* name = master_prestige ? "payroll_officer_masterprestige" : "payroll_officer";
		return automatic(name, 0, master_prestige ? 300 : 200);
	}

	std::vector<achievement_record> initial_records()
	{
		std::vector<achievement_record> records{payroll(false), payroll(true)};

		const auto catalog = loot_catalog::get_snapshot();
		if (!catalog)
		{
			return records;
		}

		for (std::size_t rank = 0; rank < catalog->social_ranks.size(); ++rank)
		{
			records.push_back(social_rank(rank + 1, catalog->social_ranks[rank]));
		}

		return records;
	}

	bool process(const reward_game_events::event& event, const std::uint64_t user,
		const std::uint32_t timestamp, const loot_catalog::catalog* catalog, std::string& push)
	{
		push.clear();

		const auto is_payroll = event.name == "picked_up_payroll";
		if (!is_payroll && event.name != "social_score")
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

		achievement_record record{};
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
			record = social_rank(*value, reward);
		}

		if (event.timestamp <= 0)
		{
			return true;
		}

		const auto saved = achievement_store::complete_hq_reward(record, timestamp, event_seconds(event.timestamp), is_payroll,
			[&](marketplace_store::transaction& economy, const achievement_record& completed, const bool grant)
		{
			if (!is_payroll && economy.get_currency(social_score_currency) < threshold)
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
