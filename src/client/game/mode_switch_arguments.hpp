#pragma once

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace mode_switch::arguments
{
	inline std::wstring lower(std::wstring value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](const wchar_t c)
		{
			return static_cast<wchar_t>(std::towlower(c));
		});

		return value;
	}

	inline bool is_option(const std::wstring& value)
	{
		return !value.empty() && (value.front() == L'-' || value.front() == L'+');
	}

	inline std::vector<std::wstring> retain(const std::vector<std::wstring>& values)
	{
		std::vector<std::wstring> result;
		for (size_t i = 0; i < values.size(); ++i)
		{
			const auto option = lower(values[i]);
			if (option == L"-singleplayer" || option == L"-multiplayer" || option == L"-zombies"
				|| option == L"-dedicated" || option == L"-hq")
			{
				continue;
			}

			if (option == L"+set" && i + 1 < values.size())
			{
				const auto dvar = lower(values[i + 1]);
				if (dvar == L"zombiesmode" || dvar == L"coremode" || dvar == L"enterhub"
					|| dvar == L"enterlocal" || dvar == L"enterlocalzombies" || dvar == L"dedicated")
				{
					++i;
					if (i + 1 < values.size() && !is_option(values[i + 1])) ++i;
					continue;
				}
			}

			if (option == L"+zombiesmode" || option == L"+coremode" || option == L"+enterhub"
				|| option == L"+enterlocal" || option == L"+enterlocalzombies" || option == L"-profile"
				|| option == L"-wait-for-parent" || option == L"+connect" || option == L"+connect_lobby"
				|| option == L"+map" || option == L"+devmap")
			{
				if (i + 1 < values.size() && !is_option(values[i + 1])) ++i;
				continue;
			}

			result.push_back(values[i]);
		}
		return result;
	}

	inline std::wstring quote(const std::wstring& value)
	{
		// The engine's startup parser needs unquoted +command tokens.
		if (!value.empty() && value.find_first_of(L" \t\r\n\v\"") == std::wstring::npos)
		{
			return value;
		}

		std::wstring result = L"\"";
		size_t backslashes = 0;

		for (const auto c : value)
		{
			if (c == L'\\')
			{
				++backslashes;
				continue;
			}

			result.append(backslashes * (c == L'"' ? 2 : 1), L'\\');
			backslashes = 0;

			if (c == L'"')
			{
				result += L'\\';
			}

			result += c;
		}

		result.append(backslashes * 2, L'\\');
		return result + L'"';
	}
}
