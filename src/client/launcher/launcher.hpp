#pragma once
#include "html/html_window.hpp"
#include "game/game.hpp"

#include <optional>

class launcher final
{
public:
	launcher();

	std::optional<game::environment::mode> run() const;

private:
	std::optional<game::environment::mode> mode_{};
	html_window main_window_;

	void select_mode(game::environment::mode mode);
	void create_main_menu();

	static std::string load_content(int res);
};
