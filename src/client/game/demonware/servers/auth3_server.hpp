#pragma once
#include "http_server.hpp"
#include "../reply.hpp"

namespace demonware
{
	class auth3_server : public http_server
	{
	public:
		using http_server::http_server;

	private:
		void send_reply(reply* data);
		void handle_request(const http_request& request) override;
	};
}
