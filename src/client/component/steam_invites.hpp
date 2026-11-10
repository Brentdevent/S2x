#pragma once

#include <cstdint>
#include <string_view>

namespace steam_invites
{
	// Called by the native friends UI and Steam callback pump on the main thread.
	bool invite_friend(std::uint64_t friend_id);
	void accept_invite(std::string_view connect_string);
}
