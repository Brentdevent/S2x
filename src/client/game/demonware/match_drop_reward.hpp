#pragma once

#include "marketplace_store.hpp"

#include <span>

namespace demonware::match_drop_reward
{
	inline constexpr auto command = "$s2x_drop";
	inline constexpr unsigned maximum_drops = 18;

	struct award
	{
		std::uint64_t user{};
		std::string match;
		std::uint32_t item_id{};
	};

	std::vector<award> distribute(std::span<const std::uint64_t> users, unsigned total, const std::string& match);
	std::optional<award> parse(std::string_view user, std::string_view match, std::string_view item_id);
	marketplace_store::transaction_result grant(const award& value);
}
