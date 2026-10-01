#include <std_include.hpp>

#include "loot_store.hpp"

#include <utils/io.hpp>
#include <utils/finally.hpp>

#include <mutex>

namespace demonware::loot_store
{
	namespace
	{
		constexpr auto loot_file = "players2/user/loot.json";

		std::mutex loot_mutex{};
		state loot{};
		bool loot_loaded{};

		void load_map(const rapidjson::Value& document, const char* name,
			std::map<std::uint32_t, std::uint32_t>& target)
		{
			if (!document.HasMember(name) || !document[name].IsObject())
			{
				return;
			}

			const auto& object = document[name];
			for (auto member = object.MemberBegin(); member != object.MemberEnd(); ++member)
			{
				if (!member->value.IsUint())
				{
					continue;
				}

				char* end{};
				const auto key = std::strtoul(member->name.GetString(), &end, 10);
				if (end == member->name.GetString() || *end || key > std::numeric_limits<std::uint32_t>::max())
				{
					continue;
				}

				if (const auto value = member->value.GetUint())
				{
					target[static_cast<std::uint32_t>(key)] = value;
				}
			}
		}

		void load_loot()
		{
			if (loot_loaded)
			{
				return;
			}

			loot_loaded = true;
			std::string data{};
			if (!utils::io::read_file(loot_file, &data))
			{
				return;
			}

			rapidjson::Document document{};
			document.Parse(data.data(), data.size());
			if (document.HasParseError() || !document.IsObject())
			{
				return;
			}

			load_map(document, "items", loot.items);
			load_map(document, "currencies", loot.currencies);

			if (document.HasMember("lastLoginDay") && document["lastLoginDay"].IsInt64())
			{
				loot.last_login_day = document["lastLoginDay"].GetInt64();
			}

			if (document.HasMember("loginStreak") && document["loginStreak"].IsUint())
			{
				loot.login_streak = document["loginStreak"].GetUint();
			}
		}

		rapidjson::Value save_map(const std::map<std::uint32_t, std::uint32_t>& source,
			rapidjson::Document::AllocatorType& allocator)
		{
			rapidjson::Value object{rapidjson::kObjectType};
			for (const auto& [key, value] : source)
			{
				if (!value)
				{
					continue;
				}

				const auto text = std::to_string(key);
				rapidjson::Value name{text.data(), static_cast<rapidjson::SizeType>(text.size()), allocator};
				object.AddMember(name, rapidjson::Value{value}, allocator);
			}

			return object;
		}

		bool save_loot()
		{
			rapidjson::Document document{};
			document.SetObject();
			auto& allocator = document.GetAllocator();
			document.AddMember("items", save_map(loot.items, allocator), allocator);
			document.AddMember("currencies", save_map(loot.currencies, allocator), allocator);
			document.AddMember("lastLoginDay", loot.last_login_day, allocator);
			document.AddMember("loginStreak", loot.login_streak, allocator);

			rapidjson::StringBuffer buffer{};
			rapidjson::Writer<rapidjson::StringBuffer> writer{buffer};
			document.Accept(writer);
			try
			{
				const std::filesystem::path target{loot_file};
				auto temporary = target;
				temporary += "." + std::to_string(GetCurrentProcessId()) + ".tmp";
				std::filesystem::create_directories(target.parent_path());
				const auto cleanup = utils::finally([&temporary]
				{
					std::error_code error{};
					std::filesystem::remove(temporary, error);
				});
				std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
				stream.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
				stream.close();
				if (!stream)
				{
					return false;
				}

				return MoveFileExW(temporary.c_str(), target.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
			}
			catch (const std::filesystem::filesystem_error&)
			{
				return false;
			}
		}
	}

	state get()
	{
		std::lock_guard lock{loot_mutex};
		load_loot();
		return loot;
	}

	bool mutate(const std::function<bool(state&)>& mutator)
	{
		if (!mutator)
		{
			return false;
		}

		std::lock_guard lock{loot_mutex};
		load_loot();

		auto updated = loot;
		if (!mutator(updated))
		{
			return false;
		}

		std::erase_if(updated.items, [](const auto& entry) { return !entry.second; });
		std::erase_if(updated.currencies, [](const auto& entry) { return !entry.second; });

		auto original = std::move(loot);
		loot = std::move(updated);
		if (save_loot())
		{
			return true;
		}

		loot = std::move(original);
		return false;
	}
}
