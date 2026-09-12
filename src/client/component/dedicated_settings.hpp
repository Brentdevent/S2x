#pragma once

#include <string>

namespace dedicated_settings
{
	// Marks a config file (as passed to exec) whose dvar writes are admin
	// settings that must survive map rotations.
	void register_exec_file(const std::string& name);

	// Re-applies every recorded admin dvar value. Returns how many were written.
	int restore(const char* reason);

	// Logs the live scr_<gametype>_* limit values for the given gametype.
	void log_gametype_values(const std::string& gametype);
}
