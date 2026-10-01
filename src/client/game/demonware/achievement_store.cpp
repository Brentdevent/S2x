#include <std_include.hpp>

#include "achievement_store.hpp"

#include <utils/io.hpp>
#include <utils/finally.hpp>

#include <map>
#include <mutex>
#include <optional>

namespace demonware::achievement_store
{
	namespace
	{
		constexpr auto achievement_file = "players2/user/achievements.json";

		std::mutex achievement_mutex{};
		std::map<std::string, achievement_record> achievements{};
		bool achievements_loaded{};

		std::optional<achievement_status> parse_status(const rapidjson::Value& value)
		{
			if (!value.IsString())
			{
				return std::nullopt;
			}

			const std::string_view status{value.GetString(), value.GetStringLength()};
			if (status == "inactive")
			{
				return achievement_status::inactive;
			}

			if (status == "inProgress")
			{
				return achievement_status::in_progress;
			}

			if (status == "claimable")
			{
				return achievement_status::claimable;
			}

			if (status == "finished")
			{
				return achievement_status::finished;
			}

			return std::nullopt;
		}

		void load_achievements()
		{
			if (achievements_loaded)
			{
				return;
			}

			achievements_loaded = true;
			std::string data{};
			if (!utils::io::read_file(achievement_file, &data))
			{
				return;
			}

			rapidjson::Document document{};
			document.Parse(data.data(), data.size());
			if (document.HasParseError() || !document.IsObject() ||
				!document.HasMember("achievements") || !document["achievements"].IsArray())
			{
				return;
			}

			for (const auto& value : document["achievements"].GetArray())
			{
				if (!value.IsObject() || !value.HasMember("name") || !value["name"].IsString() ||
					!value.HasMember("progress") || !value["progress"].IsUint())
				{
					continue;
				}

				const auto progress = value["progress"].GetUint();
				if (progress > std::numeric_limits<std::uint16_t>::max())
				{
					continue;
				}

				achievement_record record{};
				record.name.assign(value["name"].GetString(), value["name"].GetStringLength());
				record.progress = static_cast<std::uint16_t>(progress);
				record.progress_target = std::max<std::uint16_t>(record.progress, 1);
				if (record.name.empty())
				{
					continue;
				}

				if (value.HasMember("kind") && value["kind"].IsInt())
				{
					record.kind = value["kind"].GetInt();
				}

				if (value.HasMember("fulfilledTimes") && value["fulfilledTimes"].IsInt())
				{
					record.fulfilled_times = value["fulfilledTimes"].GetInt();
				}

				if (value.HasMember("progressTarget") && value["progressTarget"].IsUint())
				{
					const auto progress_target = value["progressTarget"].GetUint();
					if (progress_target > 0 &&
						progress_target <= std::numeric_limits<std::uint16_t>::max())
					{
						record.progress_target = static_cast<std::uint16_t>(progress_target);
					}
				}

				if (value.HasMember("completionTimestamp") && value["completionTimestamp"].IsUint64())
				{
					record.completion_timestamp = value["completionTimestamp"].GetUint64();
				}

				const auto read_uint64 = [&](const char* key) -> std::uint64_t
				{
					return value.HasMember(key) && value[key].IsUint64() ? value[key].GetUint64() : 0;
				};

				const auto read_uint = [&](const char* key) -> std::uint32_t
				{
					return value.HasMember(key) && value[key].IsUint() ? value[key].GetUint() : 0;
				};

				record.requires_claim = value.HasMember("requiresClaim") && value["requiresClaim"].IsBool() &&
					value["requiresClaim"].GetBool();
				record.activation_timestamp = read_uint64("activationTimestamp");
				record.expiration_timestamp = read_uint64("expirationTimestamp");
				record.usage_time_target = read_uint("usageTimeTarget");
				record.reward_item = read_uint("rewardItem");
				record.reward_currency = read_uint("rewardCurrency");
				record.reward_amount = read_uint("rewardAmount");

				record.status = record.fulfilled_times > 0
					? achievement_status::finished
					: achievement_status::in_progress;
				if (value.HasMember("status"))
				{
					if (const auto status = parse_status(value["status"]))
					{
						record.status = *status;
					}
				}

				achievements[record.name] = std::move(record);
			}
		}

		bool save_achievements()
		{
			rapidjson::Document document{};
			document.SetObject();
			auto& allocator = document.GetAllocator();
			rapidjson::Value array{rapidjson::kArrayType};

			for (const auto& [name, record] : achievements)
			{
				rapidjson::Value value{rapidjson::kObjectType};
				value.AddMember("name",
					rapidjson::Value{name.data(), static_cast<rapidjson::SizeType>(name.size()), allocator},
					allocator);
				value.AddMember("kind", record.kind, allocator);
				value.AddMember("progress", record.progress, allocator);
				value.AddMember("progressTarget", record.progress_target, allocator);
				value.AddMember("fulfilledTimes", record.fulfilled_times, allocator);
				value.AddMember("completionTimestamp", record.completion_timestamp, allocator);
				value.AddMember("status", rapidjson::Value{get_achievement_status_name(record.status),
					allocator}, allocator);
				value.AddMember("requiresClaim", record.requires_claim, allocator);
				value.AddMember("activationTimestamp", record.activation_timestamp, allocator);
				value.AddMember("expirationTimestamp", record.expiration_timestamp, allocator);
				value.AddMember("usageTimeTarget", record.usage_time_target, allocator);
				value.AddMember("rewardItem", record.reward_item, allocator);
				value.AddMember("rewardCurrency", record.reward_currency, allocator);
				value.AddMember("rewardAmount", record.reward_amount, allocator);
				array.PushBack(value, allocator);
			}

			document.AddMember("achievements", array, allocator);
			rapidjson::StringBuffer buffer{};
			rapidjson::Writer<rapidjson::StringBuffer> writer{buffer};
			document.Accept(writer);
			try
			{
				const std::filesystem::path target{achievement_file};
				// Saves are serialized by achievement_mutex. Use a process-specific
				// sibling so a failed write never truncates the last successful save.
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

		void merge_record(achievement_record record, const std::uint64_t timestamp)
		{
			if (record.name.empty())
			{
				return;
			}

			if (!record.progress_target)
			{
				record.progress_target = std::max<std::uint16_t>(record.progress, 1);
			}

			if (record.status == achievement_status::finished && record.fulfilled_times > 0 &&
				!record.completion_timestamp)
			{
				record.completion_timestamp = timestamp;
			}

			achievements[record.name] = std::move(record);
		}
	}

	std::vector<achievement_record> get_all()
	{
		std::lock_guard lock{achievement_mutex};
		load_achievements();

		std::vector<achievement_record> result{};
		result.reserve(achievements.size());
		for (const auto& [name, record] : achievements)
		{
			result.push_back(record);
		}

		return result;
	}

	bool merge(const std::vector<achievement_record>& records)
	{
		std::lock_guard lock{achievement_mutex};
		load_achievements();

		const auto original = achievements;
		const auto timestamp = static_cast<std::uint64_t>(time(nullptr));
		for (auto record : records)
		{
			merge_record(std::move(record), timestamp);
		}

		if (save_achievements())
		{
			return true;
		}

		achievements = original;
		return false;
	}

	mutation_result mutate(const std::string& name,
		const std::function<bool(achievement_record&)>& mutator)
	{
		if (name.empty() || !mutator)
		{
			return mutation_result::unchanged;
		}

		std::lock_guard lock{achievement_mutex};
		load_achievements();

		const auto entry = achievements.find(name);
		const auto existed = entry != achievements.end();
		achievement_record original{};
		achievement_record updated{};
		if (existed)
		{
			original = entry->second;
			updated = original;
		}
		else
		{
			updated.name = name;
		}

		if (!mutator(updated))
		{
			return mutation_result::unchanged;
		}

		updated.name = name;
		achievements[name] = std::move(updated);
		if (save_achievements())
		{
			return mutation_result::updated;
		}

		if (existed)
		{
			achievements[name] = std::move(original);
		}
		else
		{
			achievements.erase(name);
		}

		return mutation_result::save_failed;
	}

	bool erase(const std::string& name)
	{
		std::lock_guard lock{achievement_mutex};
		load_achievements();

		const auto entry = achievements.find(name);
		if (entry == achievements.end())
		{
			return true;
		}

		auto original = std::move(entry->second);
		achievements.erase(entry);
		if (save_achievements())
		{
			return true;
		}

		achievements[name] = std::move(original);
		return false;
	}
}

const char* demonware::get_achievement_status_name(const achievement_status status)
{
	switch (status)
	{
	case achievement_status::inactive:
		return "inactive";
	case achievement_status::in_progress:
		return "inProgress";
	case achievement_status::claimable:
		return "claimable";
	case achievement_status::finished:
	default:
		return "finished";
	}
}
