#pragma once

#include "http_server.hpp"

namespace demonware
{
	class glutton_server final : public http_server
	{
	public:
		using http_server::http_server;

	private:
		void handle_request(const http_request& request) override;
	};
}
