#include <std_include.hpp>
#include "../dw_include.hpp"

#include "umbrella_server.hpp"

#include "component/console/console.hpp"
#include "game/demonware/identity_response.hpp"

namespace demonware
{
	void umbrella_server::handle_request(const http_request& http_request)
	{
		if (http_request.target.find("/regulations/") != std::string::npos)
		{
			rapidjson::Document response{};
			response.SetObject();
			auto& allocator = response.GetAllocator();
			response.AddMember("regulations", rapidjson::Value{rapidjson::kArrayType}, allocator);
			send_json(response);
			return;
		}

		if (http_request.target.find("/optouts/") != std::string::npos)
		{
			rapidjson::Document response{};
			response.SetObject();
			auto& allocator = response.GetAllocator();
			response.AddMember("optouts", rapidjson::Value{rapidjson::kArrayType}, allocator);
			send_json(response);
			return;
		}

		rapidjson::Document request{};
		request.Parse(http_request.body.data(), http_request.body.size());
		if (request.HasParseError() || !request.IsObject())
		{
			console::error("[DW]: [umbrella]: received an invalid request.\n");
			return;
		}

		const auto is_lsg_token_request =
			http_request.target.find("/tokens/lsg/") != std::string::npos ||
			(request.HasMember("ticket") && request.HasMember("initialVectorSeed") &&
				request.HasMember("titleID"));
		if (!is_lsg_token_request)
		{
			console::demonware("[DW]: [umbrella]: unhandled target '%s'.\n",
				http_request.target.c_str());
			return;
		}

		const auto response = identity_response::make_umbrella_lsg_token();
		send_json(response);
	}
}
