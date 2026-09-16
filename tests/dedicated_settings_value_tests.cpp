#include "../src/client/component/dedicated_settings_value.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main()
{
	using dedicated_settings::detail::parse_bool_value;
	int failures = 0;
	const auto check = [&](const bool condition, const char* label)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << label << '\n';
			++failures;
		}
	};
	// All oracle inputs are representable as int: never call atoi on overflow.
	for (const auto* text : {"01", "1", "0001", "+1", "-1", "2", "-2",
		"0", "00", "-0", "+000", "  \t\r\n\f\v01", "1 ", "1junk", "0junk",
		"1.5", "0.5", "1e3", "0x10", "010", "", " ", "+", "-", "+ 1",
		"--1", "true", "TRUE", "false", "yes", "2147483647", "-2147483648"})
	{
		const auto parsed = parse_bool_value(text);
		check(parsed.has_value(), text);
		check(parsed && *parsed == (std::atoi(text) != 0), text);
	}
	check(parse_bool_value("01") == true, "01 matches current true and falls back to true");
	check(parse_bool_value("00") == false, "00 matches current false and falls back to false");
	for (const auto* text : {"2147483648", "-2147483649", "4294967296", "999999999999999999999",
		"+2147483648tail", "-0002147483649"})
	{
		check(!parse_bool_value(text).has_value(), text);
	}
	check(parse_bool_value(std::string(1022, '0') + '1') == true, "1023-byte decimal prefix");
	check(parse_bool_value(std::string(1023, '0')) == false, "1023 zeroes do not overflow");
	check(!parse_bool_value(std::string(1024, '0')), "buffer boundary declines conversion");
	check(!parse_bool_value(std::string(100000, '9')), "huge input declines conversion");
	check(parse_bool_value(std::string("1\0ignored", 9)) == true, "embedded NUL ends prefix");
	check(parse_bool_value(std::string("\0" "1", 2)) == false, "leading NUL means zero");
	for (int number = -10000; number <= 10000; ++number)
	{
		const auto text = std::to_string(number);
		check(parse_bool_value(text) == (number != 0), "signed decimal sweep");
	}
	if (failures != 0) return EXIT_FAILURE;
	std::cout << "dedicated settings value tests passed\n";
	return EXIT_SUCCESS;
}
