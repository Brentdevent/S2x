#pragma once

#include "structs.hpp"

#include <charconv>
#include <cstring>
#include <string_view>
#include <vector>

namespace game::string_table
{
	inline const char* get_cell(const StringTable* table, const int row, const int column)
	{
		if (!table || !table->values || row < 0 || row >= table->rowCount || column < 0 ||
			column >= table->columnCount)
		{
			return nullptr;
		}

		return table->values[row * table->columnCount + column].string;
	}

	inline bool parse_integer(const char* text, int& value)
	{
		if (!text || !*text)
		{
			return false;
		}

		const auto* end = text + std::strlen(text);
		const auto result = std::from_chars(text, end, value);
		return result.ec == std::errc{} && result.ptr == end;
	}

	inline int find_row(const StringTable* table, const int column, const std::string_view value)
	{
		for (auto row = 0; table && row < table->rowCount; ++row)
		{
			const auto* cell = get_cell(table, row, column);
			if (cell && value == cell)
			{
				return row;
			}
		}

		return -1;
	}

	inline std::vector<std::string_view> split_references(const char* list)
	{
		std::vector<std::string_view> result{};
		if (!list)
		{
			return result;
		}

		std::string_view remaining{list};
		while (!remaining.empty())
		{
			const auto separator = remaining.find(',');
			const auto reference = remaining.substr(0, separator);
			const auto first = reference.find_first_not_of(" \t");
			if (first != std::string_view::npos)
			{
				const auto last = reference.find_last_not_of(" \t");
				result.push_back(reference.substr(first, last - first + 1));
			}

			if (separator == std::string_view::npos)
			{
				break;
			}
			remaining.remove_prefix(separator + 1);
		}

		return result;
	}
}
