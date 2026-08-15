#pragma once

#include "tcp_server.hpp"

#include <string>
#include <unordered_map>

namespace demonware
{
	struct http_request
	{
		std::string method{};
		std::string target{};
		std::unordered_map<std::string, std::string> headers{};
		std::string body{};
	};

	class http_server : public tcp_server
	{
	public:
		using tcp_server::tcp_server;

	protected:
		virtual void handle_request(const http_request& request) = 0;
		void send_json(const rapidjson::Document& document);

	private:
		void handle(SOCKET socket, const std::string& packet) override;
		void on_disconnect(SOCKET socket) override;

		std::mutex request_buffer_mutex_{};
		std::unordered_map<SOCKET, std::string> request_buffers_{};
	};
}
