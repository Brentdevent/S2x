#pragma once

#include <string>
#include <vector>

namespace mods
{
	struct mod_info
	{
		bool valid{};
		bool supports_mp{ true };
		bool supports_zm{ true };
		std::string name;
		std::string description;
		std::string author;
		std::string version;
		std::string mode{ "mp, zm" };
	};

	std::string get_loaded_mod();
	std::vector<std::string> get_mod_list();
	mod_info get_mod_info(const std::string& name);

	void load(const std::string& name);
	void unload();
}
