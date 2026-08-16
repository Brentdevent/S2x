#include <std_include.hpp>

#include "achievement_response.hpp"

namespace demonware::achievement_response
{
	rapidjson::Value serialize_achievements(
		const std::vector<achievement_record>& achievements,
		rapidjson::Document::AllocatorType& allocator)
	{
		rapidjson::Value array{rapidjson::kArrayType};
		for (const auto& achievement : achievements)
		{
			rapidjson::Value value{rapidjson::kObjectType};
			value.AddMember("kind", achievement.kind, allocator);
			value.AddMember("name", rapidjson::Value{achievement.name.data(),
				static_cast<rapidjson::SizeType>(achievement.name.size()), allocator}, allocator);
			value.AddMember("requiresClaim", false, allocator);
			value.AddMember("progress", achievement.progress, allocator);
			value.AddMember("progressTarget", achievement.progress_target, allocator);
			value.AddMember("fulfilledTimes", achievement.fulfilled_times, allocator);
			value.AddMember("completionTimestamp", achievement.completion_timestamp, allocator);
			value.AddMember("status", rapidjson::Value{
				get_achievement_status_name(achievement.status), allocator}, allocator);
			array.PushBack(value, allocator);
		}

		return array;
	}

	std::string make_get_user_achievements_response(const std::string_view client_transaction)
	{
		rapidjson::Document response{};
		response.SetObject();
		auto& allocator = response.GetAllocator();
		response.AddMember("Version", 0, allocator);
		response.AddMember("Action", "get_user_achievements", allocator);
		response.AddMember("Status", "ok", allocator);
		response.AddMember("ClientTx", rapidjson::Value{client_transaction.data(),
			static_cast<rapidjson::SizeType>(client_transaction.size()), allocator}, allocator);

		auto achievements = serialize_achievements(achievement_store::get_all(), allocator);
		response.AddMember("Achievements", achievements, allocator);
		response.AddMember("NextPageToken", "", allocator);

		rapidjson::StringBuffer buffer{};
		rapidjson::Writer<rapidjson::StringBuffer, rapidjson::Document::EncodingType,
			rapidjson::ASCII<>> writer{buffer};
		response.Accept(writer);
		return {buffer.GetString(), buffer.GetSize()};
	}
}
