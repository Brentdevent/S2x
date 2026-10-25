#include <std_include.hpp>

#include "files.hpp"
#include "component/filesystem.hpp"
#include <utils/string.hpp>

namespace images::files
{
	namespace
	{
		struct file_index
		{
			std::uint64_t version{};
			std::unordered_map<std::string, std::string> paths;
		};

		std::atomic<std::shared_ptr<const file_index>> current;
		std::mutex refresh_mutex;
	}

	void refresh()
	{
		std::lock_guard lock(refresh_mutex);
		for (;;)
		{
			auto next = std::make_shared<file_index>();
			next->version = filesystem::search_paths_version();
			for (const auto& path : filesystem::get_search_paths())
			{
				const auto root = std::filesystem::path(path) / "images";
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
					next->paths.try_emplace(utils::string::to_lower(name.generic_string()), it->path().string());
				}
			}

			if (next->version != filesystem::search_paths_version())
			{
				continue;
			}

			current.store(std::move(next));

			return;
		}
	}

	std::optional<std::string> find(const std::string& name)
	{
		auto index = current.load();
		if (!index || index->version != filesystem::search_paths_version())
		{
			refresh();
			index = current.load();
		}

		if (index->paths.empty())
		{
			return {};
		}

		const auto entry = index->paths.find(utils::string::to_lower(name));
		if (entry == index->paths.end())
		{
			return {};
		}

		return entry->second;
	}
}
