#include <std_include.hpp>
#include "end_mission.hpp"
#include "json.hpp"

#include "game/types/demonware.hpp"

namespace demonware::reward_end_mission
{
	namespace
	{
		constexpr std::size_t maximum_json_length = 6143;
		constexpr std::array<std::int32_t, 5> allowed_usage_exclusions{8, 12, 9, 10, 11};

		bool has(const rapidjson::Value& object, const char* name, bool (rapidjson::Value::*check)() const)
		{
			const auto member = object.FindMember(name);
			return member != object.MemberEnd() && (member->value.*check)();
		}

		bool valid_usage_exclusions(const rapidjson::Value& value)
		{
			if (!value.IsArray() || value.Size() > allowed_usage_exclusions.size())
			{
				return false;
			}

			std::array<bool, allowed_usage_exclusions.size()> used{};
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

				auto& seen = used[entry - allowed_usage_exclusions.begin()];
				if (seen)
				{
					return false;
				}

				seen = true;
			}

			return true;
		}

		bool parse_request(const std::string_view json, std::string& client_tx)
		{
			if (json.empty() || json.size() > maximum_json_length)
			{
				return false;
			}

			rapidjson::Document document{};
			document.Parse(json.data(), json.size());
			if (document.HasParseError() || !reward_json::common_fields(document, action, client_tx))
			{
				return false;
			}

			const auto report_usage = document.FindMember("ReportUsageTime");
			const auto has_report_usage = report_usage != document.MemberEnd();
			if (has_report_usage && (!report_usage->value.IsBool() || report_usage->value.GetBool()))
			{
				return false;
			}

			const auto usage_exclusion = document.FindMember("UsageExclusion");
			if (usage_exclusion == document.MemberEnd() || !valid_usage_exclusions(usage_exclusion->value))
			{
				return false;
			}

			return document.MemberCount() == (has_report_usage ? 11u : 10u) &&
				has(document, "TimePlayed", &rapidjson::Value::IsInt) &&
				has(document, "ActiveTime", &rapidjson::Value::IsBool) &&
				has(document, "GameMode", &rapidjson::Value::IsString) &&
				has(document, "SubGameMode", &rapidjson::Value::IsString) &&
				has(document, "MissionInstanceId", &rapidjson::Value::IsUint) &&
				has(document, "MatchID", &rapidjson::Value::IsString);
		}

		std::string make_response(const std::string_view client_tx)
		{
			rapidjson::Document response{};
			response.SetObject();

			auto& allocator = response.GetAllocator();
			response.AddMember("Version", 0, allocator);
			response.AddMember("Action", "end_mission", allocator);
			response.AddMember("Status", "ok", allocator);
			reward_json::add_string(response, "ClientTx", client_tx, allocator);
			response.AddMember("GrantedItems", rapidjson::Value{rapidjson::kArrayType}, allocator);
			response.AddMember("GrantedCurrencies", rapidjson::Value{rapidjson::kArrayType}, allocator);
			response.AddMember("DetailedInventory", rapidjson::Value{rapidjson::kArrayType}, allocator);

			return reward_json::encode(response);
		}
	}

	reward::action_result handle(const std::string_view json)
	{
		std::string client_tx{};
		if (!parse_request(json, client_tx))
		{
			return {game::demonware::BD_REWARD_EVENTS_DATA_ERROR};
		}

		return {0, make_response(client_tx), marketplace_store::transaction_status::committed};
	}
}
