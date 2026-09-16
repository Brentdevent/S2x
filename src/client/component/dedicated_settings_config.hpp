#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

namespace dedicated_settings::detail
{
	inline std::string normalize_exec_name(std::string name)
	{
		std::replace(name.begin(), name.end(), '\\', '/');
		std::transform(name.begin(), name.end(), name.begin(), [](const unsigned char character)
		{
			return static_cast<char>(std::tolower(character));
		});
		if (!name.empty() && name.find('.') == std::string::npos) name += ".cfg";
		return name;
	}

	// Shared by startup registrations, nested exec discovery and file reads.
	class admin_config_files
	{
	public:
		bool insert(const std::string& name)
		{
			const auto key = normalize_exec_name(name);
			// The lifecycle executes this file on every MP rotation. Filename
			// tracking cannot distinguish that execution from an admin's exec,
			// so its values must never become persistent admin overrides.
			return !key.empty() && key != "default_xboxlive.cfg" && names_.insert(key).second;
		}

		bool contains(const std::string& name) const
		{
			return names_.contains(normalize_exec_name(name));
		}

		std::size_t size() const { return names_.size(); }

	private:
		std::unordered_set<std::string> names_{};
	};
}
