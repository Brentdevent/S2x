#include <std_include.hpp>

#include "reward_end_mission.hpp"
#include "reward_json.hpp"
#include "game/types/demonware.hpp"

#include <algorithm>
#include <array>

namespace demonware::reward_end_mission
{
	namespace
	{
		constexpr std::array<std::int32_t, maximum_usage_exclusions> allowed_usage_exclusions{
			8, 12, 9, 10, 11,
		};

		std::string make_response(const request& request)
		{
			rapidjson::Document response{};
			response.SetObject();
			auto& allocator = response.GetAllocator();
			response.AddMember("Version", 0, allocator);
			response.AddMember("Action", "end_mission", allocator);
			response.AddMember("Status", "ok", allocator);
			reward_json::add_string(response, "ClientTx", request.client_tx, allocator);

			response.AddMember("GrantedItems", rapidjson::Value{rapidjson::kArrayType}, allocator);
			response.AddMember("GrantedCurrencies", rapidjson::Value{rapidjson::kArrayType}, allocator);
			response.AddMember("DetailedInventory", rapidjson::Value{rapidjson::kArrayType}, allocator);

			rapidjson::StringBuffer buffer{};
			rapidjson::Writer<rapidjson::StringBuffer> writer{buffer};
			response.Accept(writer);
			return {buffer.GetString(), buffer.GetSize()};
		}

	}

	bool parse_request(const std::string_view json, request& result)
	{
		if (json.empty() || json.size() > maximum_json_length)
		{
			return false;
		}

		rapidjson::Document document{};
		document.Parse(json.data(), json.size());
		if (document.HasParseError() || !document.IsObject())
		{
			return false;
		}

		request parsed{};
		bool seen_action{};
		bool seen_version{};
		bool seen_client_tx{};
		bool seen_time_played{};
		bool seen_active_time{};
		bool seen_game_mode{};
		bool seen_sub_game_mode{};
		bool seen_usage_exclusion{};
		bool seen_report_usage_time{};
		bool seen_mission_instance_id{};
		bool seen_match_id{};

		for (auto member = document.MemberBegin(); member != document.MemberEnd(); ++member)
		{
			const std::string_view name{member->name.GetString(), member->name.GetStringLength()};
			const auto& value = member->value;
			if (name == "Action")
			{
				if (seen_action || !value.IsString() ||
					std::string_view{value.GetString(), value.GetStringLength()} != action)
				{
					return false;
				}
				seen_action = true;
			}
			else if (name == "Version")
			{
				if (seen_version || !value.IsInt() || value.GetInt() != 0)
				{
					return false;
				}
				seen_version = true;
			}
			else if (name == "ClientTx")
			{
				if (seen_client_tx || !reward_json::client_tx(value, parsed.client_tx))
				{
					return false;
				}
				parsed.client_tx.assign(value.GetString(), value.GetStringLength());
				seen_client_tx = true;
			}
			else if (name == "TimePlayed")
			{
				if (seen_time_played || !value.IsInt())
				{
					return false;
				}
				parsed.time_played = value.GetInt();
				seen_time_played = true;
			}
			else if (name == "ActiveTime")
			{
				if (seen_active_time || !value.IsBool())
				{
					return false;
				}
				parsed.active_time = value.GetBool();
				seen_active_time = true;
			}
			else if (name == "GameMode")
			{
				if (seen_game_mode || !value.IsString())
				{
					return false;
				}
				parsed.game_mode.assign(value.GetString(), value.GetStringLength());
				seen_game_mode = true;
			}
			else if (name == "SubGameMode")
			{
				if (seen_sub_game_mode || !value.IsString())
				{
					return false;
				}
				parsed.sub_game_mode.assign(value.GetString(), value.GetStringLength());
				seen_sub_game_mode = true;
			}
			else if (name == "UsageExclusion")
			{
				if (seen_usage_exclusion || !value.IsArray() ||
					value.Size() > maximum_usage_exclusions)
				{
					return false;
				}

				std::array<bool, maximum_usage_exclusions> used{};
				for (const auto& exclusion : value.GetArray())
				{
					if (!exclusion.IsInt())
					{
						return false;
					}

					const auto entry = std::ranges::find(allowed_usage_exclusions, exclusion.GetInt());
					if (entry == allowed_usage_exclusions.end())
					{
						return false;
					}

					const auto index = static_cast<std::size_t>(entry - allowed_usage_exclusions.begin());
					if (used[index])
					{
						return false;
					}

					used[index] = true;
					parsed.usage_exclusions.push_back(exclusion.GetInt());
				}
				seen_usage_exclusion = true;
			}
			else if (name == "ReportUsageTime")
			{
				if (seen_report_usage_time || !value.IsBool() || value.GetBool())
				{
					return false;
				}
				parsed.has_report_usage_time = true;
				seen_report_usage_time = true;
			}
			else if (name == "MissionInstanceId")
			{
				if (seen_mission_instance_id || !value.IsUint())
				{
					return false;
				}
				parsed.mission_instance_id = value.GetUint();
				seen_mission_instance_id = true;
			}
			else if (name == "MatchID")
			{
				if (seen_match_id || !value.IsString())
				{
					return false;
				}
				parsed.match_id.assign(value.GetString(), value.GetStringLength());
				seen_match_id = true;
			}
			else
			{
				return false;
			}
		}

		if (!seen_action || !seen_version || !seen_client_tx || !seen_time_played ||
			!seen_active_time || !seen_game_mode || !seen_sub_game_mode ||
			!seen_usage_exclusion || !seen_mission_instance_id || !seen_match_id)
		{
			return false;
		}

		result = std::move(parsed);
		return true;
	}

	reward::action_result handle(const std::string_view json)
	{
		request request;
		if (!parse_request(json, request))
		{
			return {game::demonware::BD_REWARD_EVENTS_DATA_ERROR};
		}
		// Objective and absolute usage-time saves happen at the native queue/timer
		// boundary. Acknowledging this report grants nothing and needs no receipt.
		return {0, make_response(request), marketplace_store::transaction_status::committed};
	}
}
