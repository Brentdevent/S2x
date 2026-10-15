#pragma once

#include <imgui.h>

namespace gui
{
	bool is_open();

	bool gui_key_event(int local_client_num, int key, int down);
	bool gui_char_event(int local_client_num, int key);

	void on_frame(const std::function<void()>& callback, bool always = false);
	void notification(const std::string& title, const std::string& text, std::chrono::milliseconds duration = 3s);
	void copy_to_clipboard(const std::string& text);

	void register_menu(const std::string& name, const std::string& title,
		const std::function<void()>& callback, bool always = false);
	bool* get_menu_toggle(const std::string& name);

	bool contains_lower(const char* text, const std::string& filter);
}
