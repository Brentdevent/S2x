#include <std_include.hpp>

#include "response.hpp"
#include "game/demonware/reward/json.hpp"

#include <charconv>

namespace demonware::achievement_response
{
	namespace
	{
		using reward_json::find_member;

		constexpr std::string_view local_page_token_prefix = "s2x:";
		constexpr std::size_t maximum_name_length = 128;
		constexpr std::size_t maximum_names = 10;
		constexpr int maximum_kind = 13;

		constexpr std::array<std::string_view, 8> query_members
		{
			"Version",
			"Action",
			"ClientTx",
			"AchievementStatuses",
			"AchievementKinds",
			"AchievementNames",
			"PageToken",
			"Limit",
		};

		bool has_only_query_members(const rapidjson::Value& object)
		{
			for (auto member = object.MemberBegin(); member != object.MemberEnd(); ++member)
			{
				if (std::ranges::find(query_members, reward_json::view(member->name)) == query_members.end())
				{
					return false;
				}
			}

			return true;
		}

		bool parse_page_token(const std::string_view value, std::size_t& offset)
		{
			if (!reward_json::bounded_ascii(value, maximum_page_token_length) ||
				!value.starts_with(local_page_token_prefix))
			{
				return false;
			}

			const auto encoded = value.substr(local_page_token_prefix.size());
			if (encoded.empty())
			{
				return false;
			}

			offset = 0;

			const auto end = encoded.data() + encoded.size();
			const auto result = std::from_chars(encoded.data(), end, offset);

			return result.ec == std::errc{} && result.ptr == end && offset > 0;
		}

		std::optional<std::string> serialize_bounded(const rapidjson::Document& document)
		{
			rapidjson::StringBuffer buffer{};
			rapidjson::Writer<rapidjson::StringBuffer, rapidjson::Document::EncodingType, rapidjson::ASCII<>> writer{buffer};
			document.Accept(writer);

			if (buffer.GetSize() > maximum_response_length)
			{
				return std::nullopt;
			}

			return std::string{buffer.GetString(), buffer.GetSize()};
		}

		bool is_valid_transaction(const std::string_view transaction)
		{
			return transaction.size() == maximum_client_transaction_length &&
				reward_json::bounded_ascii(transaction, maximum_client_transaction_length);
		}

		bool is_valid_record(const achievement_record& achievement)
		{
			return reward_json::bounded_ascii(achievement.name, maximum_response_length) &&
				achievement.kind >= 0 && achievement.kind <= maximum_kind &&
				achievement.fulfilled_times >= 0 && achievement.progress_target != 0 &&
				achievement.status >= achievement_status::inactive && achievement.status <= achievement_status::finished;
		}

		bool matches(const user_achievements_request& request, const achievement_record& achievement)
		{
			const auto status = request.statuses.empty() || std::ranges::find(request.statuses, achievement.status) != request.statuses.end();
			const auto kind = request.kinds.empty() || std::ranges::find(request.kinds, achievement.kind) != request.kinds.end();
			const auto name = request.names.empty() || std::ranges::find(request.names, achievement.name) != request.names.end();

			return status && kind && name;
		}

		bool read_names(const rapidjson::Value& names, std::vector<std::string>& result)
		{
			if (!names.IsArray() || names.Empty() || names.Size() > maximum_names)
			{
				return false;
			}

			for (const auto& value : names.GetArray())
			{
				if (!reward_json::ascii_string(&value, maximum_name_length))
				{
					return false;
				}

				std::string name{reward_json::view(value)};
				if (std::ranges::find(result, name) != result.end())
				{
					return false;
				}

				result.push_back(std::move(name));
			}

			return true;
		}

		bool read_statuses(const rapidjson::Value& statuses, std::vector<achievement_status>& result)
		{
			if (!statuses.IsArray() || statuses.Empty() || statuses.Size() > 4)
			{
				return false;
			}

			for (const auto& value : statuses.GetArray())
			{
				const auto status = value.IsString() ? parse_achievement_status(reward_json::view(value)) : std::nullopt;
				if (!status || std::ranges::find(result, *status) != result.end())
				{
					return false;
				}

				result.push_back(*status);
			}

			return true;
		}

		bool read_kinds(const rapidjson::Value& kinds, std::vector<int>& result)
		{
			if (!kinds.IsArray() || kinds.Empty() || kinds.Size() > maximum_kind + 1)
			{
				return false;
			}

			for (const auto& value : kinds.GetArray())
			{
				if (!value.IsInt() || value.GetInt() < 0 || value.GetInt() > maximum_kind ||
					std::ranges::find(result, value.GetInt()) != result.end())
				{
					return false;
				}

				result.push_back(value.GetInt());
			}

			return true;
		}
	}

	rapidjson::Value serialize_achievements(const std::vector<achievement_record>& achievements,
		rapidjson::Document::AllocatorType& allocator)
	{
		rapidjson::Value array{rapidjson::kArrayType};

		for (const auto& achievement : achievements)
		{
			auto value = serialize_achievement(achievement, allocator);
			if (!value.IsObject())
			{
				return {};
			}

			array.PushBack(value, allocator);
		}

		return array;
	}

	bool parse_get_user_achievements_request(const std::string_view json, user_achievements_request& request)
	{
		request = {};

		if (json.empty() || json.size() > maximum_request_length)
		{
			return false;
		}

		rapidjson::Document document{};
		document.Parse(json.data(), json.size());
		if (document.HasParseError() || !reward_json::unique_members(document) || !has_only_query_members(document) ||
			!reward_json::common_fields(document, get_user_achievements_action, request.client_transaction))
		{
			return false;
		}

		const auto* limit = find_member(document, "Limit");
		if (!limit || !limit->IsUint() || !limit->GetUint() || limit->GetUint() > maximum_page_size)
		{
			return false;
		}

		request.limit = limit->GetUint();

		const auto* names = find_member(document, "AchievementNames");
		const auto* statuses = find_member(document, "AchievementStatuses");
		const auto* kinds = find_member(document, "AchievementKinds");
		const auto* page_token = find_member(document, "PageToken");

		// Stock 0x13E540 refreshes up to ten contracts by name, separate from kind and status queries
		if (names && (statuses || kinds || !read_names(*names, request.names)))
		{
			return false;
		}

		if (statuses && !read_statuses(*statuses, request.statuses))
		{
			return false;
		}

		if (kinds && !read_kinds(*kinds, request.kinds))
		{
			return false;
		}

		if (page_token && (!page_token->IsString() || !parse_page_token(reward_json::view(*page_token), request.offset)))
		{
			return false;
		}

		return true;
	}

	bool parse_get_user_achievements_for_users_request(const std::string_view json,
		user_achievements_request& request, std::uint64_t& user_id)
	{
		request = {};
		user_id = 0;

		if (json.empty() || json.size() > maximum_request_length)
		{
			return false;
		}

		rapidjson::Document document{};
		document.Parse(json.data(), json.size());
		if (document.HasParseError() || !document.IsObject())
		{
			return false;
		}

		const auto* user_ids = find_member(document, "UserIDs");

		if (!reward_json::equals(find_member(document, "Action"), get_user_achievements_for_users_action) ||
			!reward_json::equals(find_member(document, "AccountType"), "steam") ||
			!user_ids || !user_ids->IsArray() || user_ids->Size() != 1 ||
			!(*user_ids)[0u].IsUint64() || !(*user_ids)[0u].GetUint64() ||
			!document.HasMember("AchievementStatuses") || !document.HasMember("AchievementKinds") ||
			document.HasMember("AchievementNames"))
		{
			return false;
		}

		// Stock 0x141FA0 selects one user per page, so reuse the single-user query without the routing fields
		const auto selected_user = (*user_ids)[0u].GetUint64();

		document.RemoveMember("UserIDs");
		document.RemoveMember("AccountType");
		document["Action"].SetString(get_user_achievements_action.data(), document.GetAllocator());

		const auto query = serialize_bounded(document);
		if (!query || !parse_get_user_achievements_request(*query, request))
		{
			return false;
		}

		user_id = selected_user;
		return true;
	}

	bool parse_get_scheduled_user_achievements_request(const std::string_view json, std::string& client_transaction)
	{
		client_transaction.clear();

		if (json.empty() || json.size() > maximum_request_length)
		{
			return false;
		}

		rapidjson::Document document{};
		document.Parse(json.data(), json.size());

		return !document.HasParseError() && document.IsObject() && document.MemberCount() == 3 &&
			reward_json::common_fields(document, get_scheduled_user_achievements_action, client_transaction);
	}

	std::optional<std::string> make_get_user_achievements_response(const user_achievements_request& request,
		const std::vector<achievement_record>& achievements)
	{
		if (!is_valid_transaction(request.client_transaction) ||
			!request.limit || request.limit > maximum_page_size ||
			request.offset > std::numeric_limits<std::size_t>::max() - request.limit)
		{
			return std::nullopt;
		}

		std::vector<achievement_record> page{};
		page.reserve(request.limit);

		std::size_t matching_count{};
		for (const auto& achievement : achievements)
		{
			if (!matches(request, achievement))
			{
				continue;
			}

			if (!is_valid_record(achievement))
			{
				return std::nullopt;
			}

			if (matching_count >= request.offset && page.size() < request.limit)
			{
				page.push_back(achievement);
			}

			++matching_count;
		}

		rapidjson::Document response{};
		response.SetObject();

		auto& allocator = response.GetAllocator();

		auto serialized = serialize_achievements(page, allocator);
		if (!serialized.IsArray())
		{
			return std::nullopt;
		}

		reward_json::add_string(response, "Action", get_user_achievements_action, allocator);
		response.AddMember("Achievements", serialized, allocator);
		response.AddMember("Status", "ok", allocator);
		reward_json::add_string(response, "ClientTx", request.client_transaction, allocator);

		const auto next_offset = request.offset + page.size();
		if (next_offset < matching_count)
		{
			const auto token = std::string{local_page_token_prefix} + std::to_string(next_offset);
			reward_json::add_string(response, "NextPageToken", token, allocator);
		}
		else
		{
			response.AddMember("NextPageToken", rapidjson::Value{rapidjson::kNullType}, allocator);
		}

		return serialize_bounded(response);
	}

	std::optional<std::string> make_get_user_achievements_for_users_response(const user_achievements_request& request,
		const std::uint64_t user_id, const std::vector<achievement_record>& achievements)
	{
		if (!user_id || !request.names.empty())
		{
			return std::nullopt;
		}

		const auto page = make_get_user_achievements_response(request, achievements);
		if (!page)
		{
			return std::nullopt;
		}

		rapidjson::Document response{};
		response.Parse(page->data(), page->size());

		auto& allocator = response.GetAllocator();
		response["Action"].SetString(get_user_achievements_for_users_action.data(), allocator);

		rapidjson::Value records{};
		records.Swap(response["Achievements"]);

		const auto key = std::to_string(user_id);
		response["Achievements"].SetObject();
		response["Achievements"].AddMember(rapidjson::Value{key.data(), allocator}, records, allocator);

		return serialize_bounded(response);
	}

	std::optional<std::string> make_empty_scheduled_user_achievements_response(const std::string_view client_transaction)
	{
		if (!is_valid_transaction(client_transaction))
		{
			return std::nullopt;
		}

		rapidjson::Document response{};
		response.SetObject();

		auto& allocator = response.GetAllocator();
		reward_json::add_string(response, "Action", get_scheduled_user_achievements_action, allocator);
		response.AddMember("NextPeriodStartTimes", rapidjson::Value{rapidjson::kObjectType}, allocator);
		response.AddMember("ActivationLimits", rapidjson::Value{rapidjson::kObjectType}, allocator);
		response.AddMember("Achievements", rapidjson::Value{rapidjson::kArrayType}, allocator);
		response.AddMember("Status", "ok", allocator);
		reward_json::add_string(response, "ClientTx", client_transaction, allocator);

		return serialize_bounded(response);
	}
}
