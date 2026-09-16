#pragma once

#include <string>

namespace dedicated_settings
{
	// Marks a config file (as passed to exec) whose dvar writes are admin
	// settings that must survive map rotations.
	// default_xboxlive.cfg is excluded: the lifecycle also executes that file
	// each rotation, so its values cannot safely be attributed to the admin.
	void register_exec_file(const std::string& name);

	// Re-applies every recorded admin dvar value. Returns how many were written.
	int restore(const char* reason, bool startup = false);

	// Logs the live scr_<gametype>_* limit values for the given gametype.
	void log_gametype_values(const std::string& gametype);
}
