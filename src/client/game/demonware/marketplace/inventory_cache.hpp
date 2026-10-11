#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace demonware::inventory_cache
{
	struct fetch_completion
	{
		bool succeeded{};
		std::uint32_t items_per_page{};
		std::optional<std::uint32_t> result_count{};
	};

	constexpr bool should_baseline(const fetch_completion& completion)
	{
		return completion.succeeded && completion.items_per_page && completion.result_count &&
			*completion.result_count < completion.items_per_page;
	}

	template <typename Original, typename Baseline>
	auto complete_fetch(const fetch_completion completion, Original&& original, Baseline&& baseline)
	{
		const auto result = std::forward<Original>(original)();
		if (should_baseline(completion))
		{
			std::forward<Baseline>(baseline)();
		}
		return result;
	}
}
