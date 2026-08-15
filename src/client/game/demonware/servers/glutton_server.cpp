#include <std_include.hpp>
#include "../dw_include.hpp"

#include "glutton_server.hpp"

#include "../achievement_store.hpp"

#include "component/console/console.hpp"

#include "steam/steam.hpp"

namespace demonware
{
	namespace
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

		void add_user_achievements(rapidjson::Value& users, const std::string_view user_id,
			const std::vector<achievement_record>& achievements,
			rapidjson::Document::AllocatorType& allocator)
		{
			rapidjson::Value key{user_id.data(),
				static_cast<rapidjson::SizeType>(user_id.size()), allocator};
			auto array = serialize_achievements(achievements, allocator);
			users.AddMember(key, array, allocator);
		}
	}

	void glutton_server::handle_request(const http_request& http_request)
	{
		if (http_request.target.find("/secureingest/") != std::string::npos)
		{
			rapidjson::Document response{};
			response.SetObject();
			send_json(response);
			return;
		}

		rapidjson::Document request{};
		request.Parse(http_request.body.data(), http_request.body.size());
		if (request.HasParseError() || !request.IsObject())
		{
			console::error("[DW]: [glutton]: received an invalid request.\n");
			return;
		}

		const auto* action = request.HasMember("Action") && request["Action"].IsString()
			? request["Action"].GetString()
			: "";

		rapidjson::Document response{};
		response.SetObject();
		auto& allocator = response.GetAllocator();
		response.AddMember("Version", 0, allocator);
		response.AddMember("Action", rapidjson::Value{action,
			static_cast<rapidjson::SizeType>(std::strlen(action)), allocator}, allocator);
		response.AddMember("Status", "ok", allocator);

		if (request.HasMember("ClientTx") && request["ClientTx"].IsString())
		{
			const auto& client_tx = request["ClientTx"];
			response.AddMember("ClientTx", rapidjson::Value{client_tx.GetString(),
				client_tx.GetStringLength(), allocator}, allocator);
		}

		const auto is_single_user_request = std::strcmp(action, "get_user_achievements") == 0;
		const auto is_multi_user_request =
			std::strcmp(action, "get_user_achievements_for_users") == 0;
		if (is_single_user_request || is_multi_user_request)
		{
			const auto achievements = achievement_store::get_all();
			if (is_multi_user_request)
			{
				rapidjson::Value users{rapidjson::kObjectType};
				bool added_user{};
				if (request.HasMember("UserIDs") && request["UserIDs"].IsArray())
				{
					for (const auto& user : request["UserIDs"].GetArray())
					{
						if (user.IsString())
						{
							add_user_achievements(users,
								{user.GetString(), user.GetStringLength()}, achievements, allocator);
							added_user = true;
						}
						else if (user.IsUint64())
						{
							const auto user_id = std::to_string(user.GetUint64());
							add_user_achievements(users, user_id, achievements, allocator);
							added_user = true;
						}
					}
				}

				if (!added_user)
				{
					const auto user_id = std::to_string(steam::SteamUser()->GetSteamID().bits);
					add_user_achievements(users, user_id, achievements, allocator);
				}

				response.AddMember("Achievements", users, allocator);
			}
			else
			{
				auto array = serialize_achievements(achievements, allocator);
				response.AddMember("Achievements", array, allocator);
			}

			response.AddMember("NextPageToken", "", allocator);
			console::demonware("[DW]: [glutton]: returned %zu user achievements.\n",
				achievements.size());
		}
		else
		{
			console::demonware("[DW]: [glutton]: unhandled action '%s'.\n", action);
		}

		send_json(response);
	}
}
