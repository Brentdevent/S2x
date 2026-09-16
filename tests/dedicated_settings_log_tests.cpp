#include "../src/client/component/dedicated_settings_log.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	int invalid_calls = 0;
	void invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
	{
		++invalid_calls;
	}

	template <typename... Args>
	void check_format(const char* format, Args... args)
	{
		char buffer[4096];
		const auto result = _snprintf_s(buffer, sizeof(buffer), sizeof(buffer), format, args...);
		if (result < 0 || invalid_calls != 0) throw std::runtime_error("bounded log format overflowed");
	}
}

int main()
{
	const auto old_handler = _set_invalid_parameter_handler(invalid_parameter);
	try
	{
		using namespace dedicated_settings::detail;
		for (const auto length : {std::size_t{4055}, std::size_t{1024 * 1024}})
		{
			const std::string text(length, 'x');
			const auto* value = text.c_str();
			check_format(recorded_format, value, value, value);
			check_format(dropped_format, value, value);
			check_format(tracking_format, value);
			check_format(capacity_format, value, SIZE_MAX, SIZE_MAX);
			check_format(refused_format, value);
			check_format(fallback_format, value, value);
			check_format(restored_format, -1, SIZE_MAX, value);
			check_format(live_format, value);
			check_format(listed_format, value, value);
			if (text.size() != length) throw std::runtime_error("logging changed its input");
		}
		_set_invalid_parameter_handler(old_handler);
		std::cout << "dedicated settings log tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		_set_invalid_parameter_handler(old_handler);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
