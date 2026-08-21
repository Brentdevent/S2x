#pragma once

#include <string>

namespace updater
{
	struct file_info
	{
		std::string name;
		std::string version;
		std::size_t size;
		std::string hash;
	};
}
