#pragma once

#include <string_view>

namespace custom_match
{
	int get_player_limit();
	bool progression_enabled();
	bool is_valid_gametype(std::string_view gametype);
}
