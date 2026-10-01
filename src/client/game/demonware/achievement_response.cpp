#include <std_include.hpp>

#include "achievement_response.hpp"
#include "reward_json.hpp"

#include <charconv>

namespace demonware::achievement_response
{
	namespace
	{
		constexpr std::string_view local_page_token_prefix = "s2x:";

		std::optional<achievement_status> parse_status(const std::string_view value)
		{
			if (value == "inactive")
			{
				return achievement_status::inactive;
			}

			if (value == "inProgress")
			{
				return achievement_status::in_progress;
			}

			if (value == "claimable")
			{
				return achievement_status::claimable;
			}

			if (value == "finished")
			{
				return achievement_status::finished;
			}

			return std::nullopt;
		}

		bool parse_page_token(const std::string_view value, std::size_t& offset)
		{
			if (!reward_json::bounded_ascii(value, maximum_page_token_length) ||
				!value.starts_with(local_page_token_prefix))
			{
				return false;
			}

			const auto encoded_offset = value.substr(local_page_token_prefix.size());
			if (encoded_offset.empty())
			{
				return false;
			}

			offset = 0;
			const auto result = std::from_chars(encoded_offset.data(),
				encoded_offset.data() + encoded_offset.size(), offset);
			return result.ec == std::errc{} &&
				result.ptr == encoded_offset.data() + encoded_offset.size() && offset > 0;
		}

		std::optional<std::string> serialize_bounded(const rapidjson::Document& document)
		{
			rapidjson::StringBuffer buffer{};
			rapidjson::Writer<rapidjson::StringBuffer, rapidjson::Document::EncodingType,
				rapidjson::ASCII<>> writer{buffer};
			document.Accept(writer);
			if (buffer.GetSize() > maximum_response_length)
			{
				return std::nullopt;
			}

			return std::string{buffer.GetString(), buffer.GetSize()};
		}

		bool contains_status(const std::vector<achievement_status>& statuses,
			const achievement_status status)
		{
			return statuses.empty() || std::ranges::find(statuses, status) != statuses.end();
		}

		bool contains_kind(const std::vector<int>& kinds, const int kind)
		{
			return kinds.empty() || std::ranges::find(kinds, kind) != kinds.end();
		}

		bool is_valid_record(const achievement_record& achievement)
		{
			return reward_json::bounded_ascii(achievement.name, maximum_response_length) &&
				achievement.kind >= 0 && achievement.kind <= 13 &&
				achievement.fulfilled_times >= 0 && achievement.progress_target != 0 &&
				(achievement.status == achievement_status::inactive ||
					achievement.status == achievement_status::in_progress ||
					achievement.status == achievement_status::claimable ||
					achievement.status == achievement_status::finished);
		}
	}

	rapidjson::Value serialize_achievements(
		const std::vector<achievement_record>& achievements,
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

	bool parse_get_user_achievements_request(const std::string_view json,
		user_achievements_request& request)
	{
		request = {};
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

		std::size_t version_count{};
		std::size_t action_count{};
		std::size_t transaction_count{};
		std::size_t statuses_count{};
		std::size_t kinds_count{};
		std::size_t names_count{};
		std::size_t page_token_count{};
		std::size_t limit_count{};
		for (auto member = document.MemberBegin(); member != document.MemberEnd(); ++member)
		{
			const std::string_view name{member->name.GetString(), member->name.GetStringLength()};
			if (name == "Version")
			{
				++version_count;
			}
			else if (name == "Action")
			{
				++action_count;
			}
			else if (name == "ClientTx")
			{
				++transaction_count;
			}
			else if (name == "AchievementStatuses")
			{
				++statuses_count;
			}
			else if (name == "AchievementKinds")
			{
				++kinds_count;
			}
			else if (name == "AchievementNames")
			{
				++names_count;
			}
			else if (name == "PageToken")
			{
				++page_token_count;
			}
			else if (name == "Limit")
			{
				++limit_count;
			}
			else
			{
				return false;
			}
		}

		if (version_count != 1 || action_count != 1 || transaction_count != 1 ||
			statuses_count > 1 || kinds_count > 1 || names_count > 1 || page_token_count > 1 || limit_count != 1 ||
			(names_count && (statuses_count || kinds_count)) ||
			!reward_json::common_fields(document, get_user_achievements_action,
				request.client_transaction) || !document["Limit"].IsUint() ||
			document["Limit"].GetUint() == 0 ||
			document["Limit"].GetUint() > maximum_page_size)
		{
			return false;
		}
		request.limit = document["Limit"].GetUint();

		if (names_count)
		{
			// Stock 0x13E540 -> 0x13E220 refreshes up to ten contracts by name
			// after end_mission. This query is separate from kind/status fetches.
			const auto& names = document["AchievementNames"];
			if (!names.IsArray() || names.Empty() || names.Size() > 10)
			{
				return false;
			}
			for (const auto& value : names.GetArray())
			{
				if (!value.IsString())
				{
					return false;
				}
				const std::string name{value.GetString(), value.GetStringLength()};
				if (!reward_json::bounded_ascii(name, 128) ||
					std::ranges::find(request.names, name) != request.names.end())
				{
					return false;
				}
				request.names.push_back(name);
			}
		}

		if (statuses_count)
		{
			const auto& statuses = document["AchievementStatuses"];
			if (!statuses.IsArray() || statuses.Empty() || statuses.Size() > 4)
			{
				return false;
			}

			for (const auto& value : statuses.GetArray())
			{
				if (!value.IsString())
				{
					return false;
				}

				const auto status = parse_status({value.GetString(), value.GetStringLength()});
				if (!status || std::ranges::find(request.statuses, *status) != request.statuses.end())
				{
					return false;
				}

				request.statuses.push_back(*status);
			}
		}

		if (kinds_count)
		{
			const auto& kinds = document["AchievementKinds"];
			if (!kinds.IsArray() || kinds.Empty() || kinds.Size() > 14)
			{
				return false;
			}

			for (const auto& value : kinds.GetArray())
			{
				if (!value.IsInt() || value.GetInt() < 0 || value.GetInt() > 13 ||
					std::ranges::find(request.kinds, value.GetInt()) != request.kinds.end())
				{
					return false;
				}

				request.kinds.push_back(value.GetInt());
			}
		}

		if (page_token_count)
		{
			const auto& page_token = document["PageToken"];
			if (!page_token.IsString() || !parse_page_token(
				{page_token.GetString(), page_token.GetStringLength()}, request.offset))
			{
				return false;
			}
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
		rapidjson::Document document;
		document.Parse(json.data(), json.size());
		if (document.HasParseError() || !document.IsObject() ||
			!document.HasMember("Action") || !document["Action"].IsString() ||
			std::string_view{document["Action"].GetString(), document["Action"].GetStringLength()} !=
				get_user_achievements_for_users_action ||
			!document.HasMember("UserIDs") || !document["UserIDs"].IsArray() ||
			document["UserIDs"].Size() != 1 || !document["UserIDs"][0].IsUint64() ||
			!document["UserIDs"][0].GetUint64() ||
			!document.HasMember("AccountType") || !document["AccountType"].IsString() ||
			std::string_view{document["AccountType"].GetString(), document["AccountType"].GetStringLength()} != "steam" ||
			!document.HasMember("AchievementStatuses") || !document.HasMember("AchievementKinds") ||
			document.HasMember("AchievementNames"))
		{
			return false;
		}

		// Stock 0x141FA0 selects one server user per page. Reuse the same
		// filters and pagination as Task 4 after removing its routing fields.
		const auto selected_user = document["UserIDs"][0].GetUint64();
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

	std::optional<std::string> make_get_user_achievements_for_users_response(
		const user_achievements_request& request, const std::uint64_t user_id,
		const std::vector<achievement_record>& achievements)
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
		rapidjson::Document response;
		response.Parse(page->data(), page->size());
		auto& allocator = response.GetAllocator();
		response["Action"].SetString(get_user_achievements_for_users_action.data(), allocator);
		rapidjson::Value records;
		records.Swap(response["Achievements"]);
		response["Achievements"].SetObject();
		const auto key = std::to_string(user_id);
		response["Achievements"].AddMember(rapidjson::Value{key.c_str(), allocator}, records, allocator);
		return serialize_bounded(response);
	}

	bool parse_get_scheduled_user_achievements_request(const std::string_view json,
		std::string& client_transaction)
	{
		client_transaction.clear();
		if (json.empty() || json.size() > maximum_request_length)
		{
			return false;
		}

		rapidjson::Document document{};
		document.Parse(json.data(), json.size());
		if (document.HasParseError() || !document.IsObject() || document.MemberCount() != 3)
		{
			return false;
		}

		std::size_t version_count{};
		std::size_t action_count{};
		std::size_t transaction_count{};
		for (auto member = document.MemberBegin(); member != document.MemberEnd(); ++member)
		{
			const std::string_view name{member->name.GetString(), member->name.GetStringLength()};
			if (name == "Version")
			{
				++version_count;
			}
			else if (name == "Action")
			{
				++action_count;
			}
			else if (name == "ClientTx")
			{
				++transaction_count;
			}
			else
			{
				return false;
			}
		}

		return version_count == 1 && action_count == 1 && transaction_count == 1 &&
			reward_json::common_fields(document, get_scheduled_user_achievements_action,
				client_transaction);
	}

	std::optional<std::string> make_get_user_achievements_response(
		const user_achievements_request& request,
		const std::vector<achievement_record>& achievements)
	{
		if (request.client_transaction.size() != maximum_client_transaction_length ||
			!reward_json::bounded_ascii(request.client_transaction, maximum_client_transaction_length) ||
			request.limit == 0 || request.limit > maximum_page_size ||
			request.offset > std::numeric_limits<std::size_t>::max() - request.limit)
		{
			return std::nullopt;
		}

		std::vector<achievement_record> page{};
		page.reserve(request.limit);
		std::size_t matching_count{};
		for (const auto& achievement : achievements)
		{
			if (!contains_status(request.statuses, achievement.status) ||
				!contains_kind(request.kinds, achievement.kind) ||
				(!request.names.empty() && std::ranges::find(request.names, achievement.name) == request.names.end()))
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
		response.AddMember("Action", rapidjson::Value{get_user_achievements_action.data(),
			static_cast<rapidjson::SizeType>(get_user_achievements_action.size()), allocator}, allocator);
		auto serialized = serialize_achievements(page, allocator);
		if (!serialized.IsArray())
		{
			return std::nullopt;
		}
		response.AddMember("Achievements", serialized, allocator);
		response.AddMember("Status", "ok", allocator);
		response.AddMember("ClientTx", rapidjson::Value{request.client_transaction.data(),
			static_cast<rapidjson::SizeType>(request.client_transaction.size()), allocator}, allocator);
		const auto next_offset = request.offset + page.size();
		if (next_offset < matching_count)
		{
			const auto next_page_token =
				std::string{local_page_token_prefix} + std::to_string(next_offset);
			response.AddMember("NextPageToken", rapidjson::Value{next_page_token.data(),
				static_cast<rapidjson::SizeType>(next_page_token.size()), allocator}, allocator);
		}
		else
		{
			response.AddMember("NextPageToken", rapidjson::Value{rapidjson::kNullType}, allocator);
		}

		return serialize_bounded(response);
	}

	std::optional<std::string> make_empty_scheduled_user_achievements_response(
		const std::string_view client_transaction)
	{
		if (client_transaction.size() != maximum_client_transaction_length ||
			!reward_json::bounded_ascii(client_transaction, maximum_client_transaction_length))
		{
			return std::nullopt;
		}

		rapidjson::Document response{};
		response.SetObject();
		auto& allocator = response.GetAllocator();
		response.AddMember("Action", rapidjson::Value{
			get_scheduled_user_achievements_action.data(),
			static_cast<rapidjson::SizeType>(get_scheduled_user_achievements_action.size()),
			allocator}, allocator);
		response.AddMember("NextPeriodStartTimes", rapidjson::Value{rapidjson::kObjectType},
			allocator);
		response.AddMember("ActivationLimits", rapidjson::Value{rapidjson::kObjectType}, allocator);
		response.AddMember("Achievements", rapidjson::Value{rapidjson::kArrayType}, allocator);
		response.AddMember("Status", "ok", allocator);
		response.AddMember("ClientTx", rapidjson::Value{client_transaction.data(),
			static_cast<rapidjson::SizeType>(client_transaction.size()), allocator}, allocator);
		return serialize_bounded(response);
	}

}
