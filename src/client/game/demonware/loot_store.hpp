#pragma once

#include <cstdint>
#include <functional>
#include <map>

namespace demonware::loot_store
{
	struct state
	{
		std::map<std::uint32_t, std::uint32_t> items{};
		std::map<std::uint32_t, std::uint32_t> currencies{};
		std::int64_t last_login_day{-1};
		std::uint32_t login_streak{};
	};

	state get();
	bool mutate(const std::function<bool(state&)>& mutator);
}
