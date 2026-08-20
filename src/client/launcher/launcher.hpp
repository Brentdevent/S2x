#pragma once
#include "html/html_window.hpp"
#include "game/game.hpp"

#include <optional>

class launcher final
{
public:
	launcher();

	std::optional<game::environment::mode> run() const;
	static void apply_saved_launch_options();

private:
	enum class console_mode
	{
		syscon,
		terminal,
		disabled,
	};

	struct launch_options
	{
		console_mode console{console_mode::syscon};
		bool no_steam{};
		bool no_update{};
	};

	launch_options launch_options_{};
	std::optional<game::environment::mode> mode_{};
	html_window main_window_;

	void select_mode(game::environment::mode mode);
	void create_main_menu();
	void save_launch_options() const;

	static launch_options load_launch_options();
	static void apply_launch_options(const launch_options& options);
	static std::string serialize_launch_options(const launch_options& options);
	static std::optional<console_mode> parse_console_mode(const std::string& value);
	static const char* get_console_mode_name(console_mode mode);

	static std::string load_content(int res);
};
