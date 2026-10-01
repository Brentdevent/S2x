#include <std_include.hpp>

#include "achievement_response.hpp"

namespace demonware::achievement_response
{
	rapidjson::Value serialize_rewards(const achievement_record& achievement,
		rapidjson::Document::AllocatorType& allocator)
	{
		rapidjson::Value rewards{rapidjson::kArrayType};
		if (achievement.reward_item)
		{
			rapidjson::Value item{rapidjson::kObjectType};
			item.AddMember("id", achievement.reward_item, allocator);
			item.AddMember("quantity", 1, allocator);

			rapidjson::Value items{rapidjson::kArrayType};
			items.PushBack(item, allocator);

			rapidjson::Value product{rapidjson::kObjectType};
			product.AddMember("id", achievement.reward_item, allocator);
			product.AddMember("currencies", rapidjson::Value{rapidjson::kArrayType}, allocator);
			product.AddMember("items", items, allocator);

			rapidjson::Value reward{rapidjson::kObjectType};
			reward.AddMember("type", "grant_product", allocator);
			reward.AddMember("product", product, allocator);
			rewards.PushBack(reward, allocator);
		}

		if (achievement.reward_currency && achievement.reward_amount)
		{
			rapidjson::Value currency{rapidjson::kObjectType};
			currency.AddMember("id", achievement.reward_currency, allocator);
			currency.AddMember("amount", achievement.reward_amount, allocator);

			rapidjson::Value reward{rapidjson::kObjectType};
			reward.AddMember("type", "grant_currency", allocator);
			reward.AddMember("currency", currency, allocator);
			rewards.PushBack(reward, allocator);
		}

		return rewards;
	}

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
			value.AddMember("requiresClaim", achievement.requires_claim, allocator);
			value.AddMember("progress", achievement.progress, allocator);
			value.AddMember("progressTarget", achievement.progress_target, allocator);
			value.AddMember("fulfilledTimes", achievement.fulfilled_times, allocator);
			value.AddMember("completionTimestamp", achievement.completion_timestamp, allocator);
			value.AddMember("status", rapidjson::Value{
				get_achievement_status_name(achievement.status), allocator}, allocator);

			if (achievement.activation_timestamp)
			{
				value.AddMember("activationTimestamp", achievement.activation_timestamp, allocator);
			}

			if (achievement.expiration_timestamp)
			{
				value.AddMember("expirationTimestamp", achievement.expiration_timestamp, allocator);
			}

			if (achievement.usage_time_target)
			{
				value.AddMember("usageTimeTarget", achievement.usage_time_target, allocator);
			}

			value.AddMember("successRewards", serialize_rewards(achievement, allocator), allocator);
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

		const auto now = static_cast<std::uint64_t>(time(nullptr));
		auto records = achievement_store::get_all();
		std::erase_if(records, [&](const achievement_record& record)
		{
			return record.status == achievement_status::in_progress && record.expiration_timestamp &&
				record.expiration_timestamp <= now;
		});

		auto achievements = serialize_achievements(records, allocator);
		response.AddMember("Achievements", achievements, allocator);
		response.AddMember("NextPageToken", "", allocator);

		rapidjson::StringBuffer buffer{};
		rapidjson::Writer<rapidjson::StringBuffer, rapidjson::Document::EncodingType,
			rapidjson::ASCII<>> writer{buffer};
		response.Accept(writer);
		return {buffer.GetString(), buffer.GetSize()};
	}
}
