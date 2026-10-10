#include <std_include.hpp>
#include "match_drop.hpp"
#include "game/demonware/economy_tools.hpp"

#include <utils/cryptography.hpp>
#include <utils/string.hpp>

#include <charconv>
#include <random>

namespace demonware::match_drop_reward
{
	namespace
	{
		struct drop_option
		{
			std::uint32_t item_id;
			unsigned weight;
		};

		// Local match-reward weights, not retail odds. IDs are from supplyDropTypes.csv.
		// Only repeatable MP drops: no Common, Zombies or duplicate-protected bribes.
		constexpr std::array<drop_option, 6> drop_pool
		{{
			{2, 60},  // Rare
			{75, 10}, // Winter Siege
			{77, 10}, // Resistance
			{94, 10}, // Blitzkrieg
			{74, 7},  // Legendary
			{73, 3},  // Epic
		}};

		bool valid_item(const std::uint32_t item_id)
		{
			return std::ranges::any_of(drop_pool, [item_id](const auto& drop)
			{
				return drop.item_id == item_id;
			});
		}

		bool valid_match(const std::string_view match)
		{
			return match.size() == 32 && match.find_first_not_of("0123456789abcdefABCDEF") == std::string_view::npos;
		}

		template <typename T>
		bool number(const std::string_view text, T& value, const int base)
		{
			const auto end = text.data() + text.size();
			const auto parsed = std::from_chars(text.data(), end, value, base);
			return parsed.ec == std::errc{} && parsed.ptr == end && value;
		}
	}

	std::vector<award> distribute(const std::span<const std::uint64_t> users, const unsigned total,
		const std::string& match)
	{
		if (!total || total > maximum_drops || !valid_match(match))
		{
			return {};
		}

		std::vector<award> recipients;
		for (const auto user : users)
		{
			if (user && std::ranges::none_of(recipients, [user](const auto& entry) { return entry.user == user; }))
			{
				recipients.push_back({user, utils::string::to_lower(match)});
			}
		}

		if (recipients.empty())
		{
			return {};
		}

		// Local host policy: sample distinct players without replacement. Unused
		// crates stay unawarded when the configured total exceeds the human count.
		std::mt19937 random{utils::cryptography::random::get_integer()};
		std::shuffle(recipients.begin(), recipients.end(), random);
		recipients.resize(std::min<std::size_t>(total, recipients.size()));

		unsigned total_weight{};
		for (const auto& drop : drop_pool)
		{
			total_weight += drop.weight;
		}

		std::uniform_int_distribution<unsigned> selection{0, total_weight - 1};
		for (auto& recipient : recipients)
		{
			auto roll = selection(random);
			for (const auto& drop : drop_pool)
			{
				if (roll < drop.weight)
				{
					recipient.item_id = drop.item_id;
					break;
				}

				roll -= drop.weight;
			}
		}

		return recipients;
	}

	std::optional<award> parse(const std::string_view user, const std::string_view match,
		const std::string_view item_id)
	{
		award result;
		if (user.empty() || user.size() > 16 || !number(user, result.user, 16) || !valid_match(match) ||
			item_id.empty() || item_id.size() > 10 || !number(item_id, result.item_id, 10) || !valid_item(result.item_id))
		{
			return {};
		}

		result.match = utils::string::to_lower(std::string{match});
		return result;
	}

	marketplace_store::transaction_result grant(const award& value)
	{
		if (!value.user || !valid_item(value.item_id) || !valid_match(value.match))
		{
			return {};
		}

		// Persist the host's choice once. The grant fingerprint includes the item ID,
		// so changing the crate on a replay conflicts instead of awarding it again.
		return economy_tools::give_item(value.item_id, 1, value.user,
			"matchdrop:" + utils::string::to_lower(value.match));
	}
}
