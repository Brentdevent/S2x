#include <std_include.hpp>
#include "resources.hpp"
#include "resource.hpp"

namespace launcher_resources
{
	namespace
	{
		struct resource_file
		{
			std::string_view data;
			size_t position{};
		};
	}

	std::string_view load(const std::string& name)
	{
		static constexpr std::pair<const char*, int> resources[]{
			{"launcher.rml", LAUNCHER_DOCUMENT},
			{"launcher.rcss", LAUNCHER_STYLES},
			{"title.woff", LAUNCHER_TITLE_FONT},
			{"body.ttf", LAUNCHER_BODY_FONT},
			{"background.jpg", LAUNCHER_BACKGROUND},
			{"singleplayer.png", LAUNCHER_SINGLEPLAYER},
			{"multiplayer.png", LAUNCHER_MULTIPLAYER},
			{"zombies.png", LAUNCHER_ZOMBIES},
			{"licenses.txt", LAUNCHER_LICENSES},
		};

		const auto entry = std::find_if(std::begin(resources), std::end(resources),
			[&name](const auto& item)
			{
				return name == item.first;
			});

		if (entry == std::end(resources))
		{
			return {};
		}

		const auto module = GetModuleHandleW(nullptr);
		const auto resource = FindResourceA(module, MAKEINTRESOURCEA(entry->second), RT_RCDATA);

		if (!resource)
		{
			return {};
		}

		const auto data = LoadResource(module, resource);
		const auto bytes = data ? LockResource(data) : nullptr;

		if (!bytes)
		{
			return {};
		}

		return std::string_view(static_cast<const char*>(bytes), SizeofResource(module, resource));
	}

	Rml::FileHandle file_interface::Open(const Rml::String& path)
	{
		const auto data = load(path);

		return data.empty() ? 0 : reinterpret_cast<Rml::FileHandle>(new resource_file{data});
	}

	void file_interface::Close(const Rml::FileHandle file)
	{
		delete reinterpret_cast<resource_file*>(file);
	}

	size_t file_interface::Read(void* buffer, const size_t size, const Rml::FileHandle file)
	{
		auto& entry = *reinterpret_cast<resource_file*>(file);
		const auto count = std::min(size, entry.data.size() - entry.position);
		std::memcpy(buffer, entry.data.data() + entry.position, count);
		entry.position += count;

		return count;
	}

	bool file_interface::Seek(const Rml::FileHandle file, const long offset, const int origin)
	{
		auto& entry = *reinterpret_cast<resource_file*>(file);
		int64_t base{};

		switch (origin)
		{
		case SEEK_SET:
			break;

		case SEEK_CUR:
			base = static_cast<int64_t>(entry.position);
			break;

		case SEEK_END:
			base = static_cast<int64_t>(entry.data.size());
			break;

		default:
			return false;
		}

		const auto position = base + offset;

		if (position < 0 || position > static_cast<int64_t>(entry.data.size()))
		{
			return false;
		}

		entry.position = static_cast<size_t>(position);

		return true;
	}

	size_t file_interface::Tell(const Rml::FileHandle file)
	{
		return reinterpret_cast<resource_file*>(file)->position;
	}

	size_t file_interface::Length(const Rml::FileHandle file)
	{
		return reinterpret_cast<resource_file*>(file)->data.size();
	}
}
