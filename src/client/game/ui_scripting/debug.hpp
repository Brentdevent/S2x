#pragma once
#include "game/game.hpp"

namespace ui_scripting::debug
{
	void initialize();
	void clear();
	int load_buffer(game::hks::lua_State* state, const game::hks::HksCompilerSettings* settings,
		const std::string& name, const std::string& data);
}
