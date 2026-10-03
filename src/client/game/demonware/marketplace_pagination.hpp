#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace demonware::marketplace_pagination
{
	inline constexpr std::uint32_t maximum_page_size = 500;

	struct request
	{
		std::uint32_t page_number{};
		std::uint32_t items_per_page{};

		bool operator==(const request&) const = default;
	};

	struct page_range
	{
		std::size_t first{};
		std::size_t last{};

		bool operator==(const page_range&) const = default;
	};

	// Stock task 165 writes page number first and page size second.
	inline std::optional<request> parse_wire_fields(const std::uint32_t first,
		const std::uint32_t second)
	{
		request result{first, second};
		if (result.page_number == 0 || result.items_per_page == 0 ||
			result.items_per_page > maximum_page_size)
		{
			return std::nullopt;
		}

		return result;
	}

	inline std::optional<page_range> get_page_range(const std::size_t item_count,
		const std::uint32_t items_per_page, const std::uint32_t page_number)
	{
		if (items_per_page == 0 || items_per_page > maximum_page_size || page_number == 0)
		{
			return std::nullopt;
		}

		const auto page_size = static_cast<std::size_t>(items_per_page);
		const auto page_index = static_cast<std::size_t>(page_number - 1);
		if (page_index > (std::numeric_limits<std::size_t>::max)() / page_size)
		{
			return std::nullopt;
		}

		const auto first = page_index * page_size;
		if (first >= item_count)
		{
			return page_range{first, first};
		}

		return page_range{first, first + std::min(page_size, item_count - first)};
	}
}
