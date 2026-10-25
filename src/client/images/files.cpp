#include <std_include.hpp>

#include "files.hpp"
#include <utils/nt.hpp>
#include <utils/string.hpp>

namespace images::files
{
	namespace
	{
		using file_index = std::unordered_map<std::string, std::string>;

		std::atomic<std::shared_ptr<const file_index>> current;
		std::mutex refresh_mutex;
	}

	void refresh()
	{
		std::lock_guard lock(refresh_mutex);
		auto next = std::make_shared<file_index>();

		// Loose replacements are global user cosmetics, independent of mounted mods.
		const auto root = utils::nt::library{}.get_folder() / "s2x/images";
		std::error_code error;
		std::filesystem::recursive_directory_iterator it(
			root, std::filesystem::directory_options::skip_permission_denied, error);
		const std::filesystem::recursive_directory_iterator end;
		for (; !error && it != end; it.increment(error))
		{
			if (!it->is_regular_file(error)
				|| utils::string::to_lower(it->path().extension().string()) != ".dds")
			{
				continue;
			}

			auto name = it->path().lexically_relative(root);
			name.replace_extension();
			next->try_emplace(utils::string::to_lower(name.generic_string()), it->path().string());
		}

		current.store(std::move(next));
	}

	std::optional<std::string> find(const std::string& name)
	{
		auto index = current.load();
		if (!index)
		{
			refresh();
			index = current.load();
		}

		if (index->empty())
		{
			return {};
		}

		const auto entry = index->find(utils::string::to_lower(name));
		if (entry == index->end())
		{
			return {};
		}

		return entry->second;
	}
}
