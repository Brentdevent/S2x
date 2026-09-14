#pragma once

#include <utils/flags.hpp>

#include <functional>
#include <string>

namespace console
{
	void set_title(const std::string& title);

	// Transforms of text entered into the server/system console, applied in
	// registration order before the text is queued for execution; a callback
	// may rewrite the text it is given.
	void on_input(const std::function<void(std::string& text)>& callback);
	std::string notify_input(std::string text);

	enum print_type
	{
		print_type_error = 1,
		print_type_debug = 2,
		print_type_warning = 3,
		print_type_important = 4,
		print_type_redudant = 5,
		print_type_demonware = 6, // only when a flags is enabled
		print_type_info = 7
	};

	void dispatch_message(int type, const std::string& message);

	void print(int type, const char* fmt, ...);

	template <typename... Args>
	void error(const char* fmt, Args&&... args)
	{
		print(print_type_error, fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void debug(const char* fmt, Args&&... args)
	{
#ifdef DEBUG
		print(print_type_debug, fmt, std::forward<Args>(args)...);
#endif
	}

	template <typename... Args>
	void warn(const char* fmt, Args&&... args)
	{
		print(print_type_warning, fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void info(const char* fmt, Args&&... args)
	{
		print(print_type_info, fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void important(const char* fmt, Args&&... args)
	{
		print(print_type_important, fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void redudant(const char* fmt, Args&&... args)
	{
		print(print_type_redudant, fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void demonware(const char* fmt, Args&&... args)
	{
		print(print_type_demonware, fmt, std::forward<Args>(args)...);
	}

	void init();
}