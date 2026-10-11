#include <std_include.hpp>

#include "identity_response.hpp"

#include "runtime_context.hpp"

namespace demonware::identity_response
{
	namespace
	{
		using identity_details = runtime_context::identity;

		rapidjson::Document make_common_token_response(const identity_details& details)
		{
			rapidjson::Document response{};
			response.SetObject();
			auto& allocator = response.GetAllocator();

			response.AddMember("accessToken", "S2x", allocator);
			response.AddMember("umbrellaID", details.user_id, allocator);
			response.AddMember("expires",
				(static_cast<std::uint64_t>(time(nullptr)) + 86400) * 1000, allocator);
			return response;
		}

		void add_accounts(rapidjson::Document& response, const identity_details& details)
		{
			auto& allocator = response.GetAllocator();

			rapidjson::Value accounts{rapidjson::kArrayType};
			rapidjson::Value uno_account{rapidjson::kObjectType};
			uno_account.AddMember("provider", "uno", allocator);
			uno_account.AddMember("accountID", details.user_id, allocator);
			uno_account.AddMember("secondaryAccountID", details.user_id, allocator);
			uno_account.AddMember("authorized", true, allocator);
			uno_account.AddMember("username", rapidjson::Value{details.persona_name.data(),
				static_cast<rapidjson::SizeType>(details.persona_name.size()), allocator}, allocator);
			accounts.PushBack(uno_account, allocator);

			rapidjson::Value steam_account{rapidjson::kObjectType};
			steam_account.AddMember("provider", "steam", allocator);
			steam_account.AddMember("accountID", details.user_id, allocator);
			steam_account.AddMember("secondaryAccountID", details.user_id, allocator);
			steam_account.AddMember("authorized", true, allocator);
			steam_account.AddMember("username", rapidjson::Value{details.persona_name.data(),
				static_cast<rapidjson::SizeType>(details.persona_name.size()), allocator}, allocator);
			accounts.PushBack(steam_account, allocator);
			response.AddMember("accounts", accounts, allocator);
		}
	}

	rapidjson::Document make_umbrella_lsg_token()
	{
		const auto details = runtime_context::get_snapshot();
		if (!details)
		{
			return {};
		}
		auto response = make_common_token_response(*details);
		add_accounts(response, *details);
		return response;
	}

	rapidjson::Document make_uno_identity_token()
	{
		const auto details = runtime_context::get_snapshot();
		if (!details)
		{
			return {};
		}
		auto response = make_common_token_response(*details);
		auto& allocator = response.GetAllocator();
		response.AddMember("unoID", details->user_id, allocator);
		rapidjson::Value subscriptions{rapidjson::kObjectType};
		subscriptions.AddMember("call_of_duty_news", rapidjson::Value{rapidjson::kObjectType}, allocator);
		response.AddMember("subscriptions", subscriptions, allocator);
		add_accounts(response, *details);
		return response;
	}
}
