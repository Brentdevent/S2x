#pragma once

namespace command { class params; }

namespace match_drops
{
	bool receive(unsigned local_client, const command::params& args);
}
