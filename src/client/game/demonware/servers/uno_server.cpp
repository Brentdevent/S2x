#include <std_include.hpp>
#include "../dw_include.hpp"

#include "uno_server.hpp"

#include "component/console/console.hpp"
#include "game/demonware/identity_response.hpp"

namespace demonware
{
	void uno_server::handle_request(const http_request& http_request)
	{
		rapidjson::Document request{};
		request.Parse(http_request.body.data(), http_request.body.size());
		if (request.HasParseError() || !request.IsObject())
		{
			console::error("[DW]: [uno]: received an invalid request.\n");
			return;
		}

		if (http_request.target.find("/tokens/") == std::string::npos)
		{
			console::demonware("[DW]: [uno]: unhandled target '%s'.\n",
				http_request.target.c_str());
			return;
		}

		console::demonware("[DW]: [uno]: issued an identity token.\n");
		const auto response = identity_response::make_uno_identity_token();
		send_json(response);
	}
}
