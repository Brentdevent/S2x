#pragma once

#include <string_view>

namespace game::environment
{
	enum class mode;
}

namespace mode_switch
{
	void wait_for_previous_process();
	bool join_invite(game::environment::mode mode, std::string_view secret, bool discord_invite = false);
}
