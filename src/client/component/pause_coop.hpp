#pragma once

namespace command
{
	class params;
}

namespace pause::coop
{
	// Called by the existing native server-command/disconnect dispatch hooks.
	bool receive(unsigned int local_client, const command::params& args);
	void disconnect();
	// Called after native key processing, preserving key state and existing menus.
	void key_event(int local_client, int key, int down, int catchers_before);
}
