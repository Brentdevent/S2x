#include <std_include.hpp>
#include "match_drop_reward.hpp"
#include "economy_tools.hpp"
#include "loot_compatibility.hpp"

#include <utils/cryptography.hpp>
#include <utils/string.hpp>

#include <charconv>
#include <random>

namespace demonware::match_drop_reward
{
	namespace
	{
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
				recipients.push_back({user, utils::string::to_lower(match), 0});
			}
		}
		if (recipients.empty())
		{
			return {};
		}

		// Local host policy, not retail odds: one independent human recipient per
		// crate. The configured total is shared by the lobby, with repeat winners.
		std::mt19937 random{utils::cryptography::random::get_integer()};
		std::uniform_int_distribution<std::size_t> draw{0, recipients.size() - 1};
		for (unsigned i = 0; i < total; ++i)
		{
			++recipients[draw(random)].quantity;
		}
		std::erase_if(recipients, [](const auto& entry) { return !entry.quantity; });
		return recipients;
	}

	std::optional<award> parse(const std::string_view user, const std::string_view match,
		const std::string_view quantity)
	{
		award result;
		if (user.empty() || user.size() > 16 || !number(user, result.user, 16) || !valid_match(match) ||
			!number(quantity, result.quantity, 10) || result.quantity > maximum_drops)
		{
			return {};
		}
		result.match = utils::string::to_lower(std::string{match});
		return result;
	}

	marketplace_store::transaction_result grant(const award& value)
	{
		if (!value.user || !value.quantity || value.quantity > maximum_drops || !valid_match(value.match))
		{
			return {};
		}

		// supplyDropTypes.csv: sd_mp_rare is inventory item 2. The existing grant
		// transaction preserves metadata and atomically saves stock with its receipt.
		return economy_tools::give_item(loot_compatibility::mp_supply_drop_tiers[0].item_id,
			value.quantity, value.user, "matchdrop:" + utils::string::to_lower(value.match));
	}
}
