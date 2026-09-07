#pragma once

#include <string_view>
#include <unordered_map>

namespace game::lookup::detail
{
	using string_map = std::unordered_map<std::string_view, std::string_view>;

	template <typename Map>
	inline std::string_view find_or_self(const Map& table, const std::string_view value)
	{
		if (const auto itr = table.find(value); itr != table.end())
		{
			return itr->second;
		}

		return value;
	}
}
