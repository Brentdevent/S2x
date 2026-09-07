#pragma once

#include "game/game.hpp"

#include <string>

namespace dvars
{
	namespace override
	{
		// Declare overrides during post_unpack, before the engine registers its dvars.
		void register_float(const std::string& name, float value, float min, float max, game::DvarFlags flags);

		// Archive a local preference and ignore script setclientdvar(s) writes to it.
		// Declare this on clients and dedicated servers so their NETWORK tables agree.
		void register_local_float(const std::string& name, float value, float min, float max);

		bool is_local(const char* name);
	}
}
