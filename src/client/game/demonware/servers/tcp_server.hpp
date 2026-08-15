#pragma once

#include "base_server.hpp"
#include <utils/concurrency.hpp>

namespace demonware
{
	class tcp_server : public base_server
	{
	public:
		using base_server::base_server;

		void handle_input(SOCKET socket, const char* buf, size_t size);
		size_t handle_output(SOCKET socket, char* buf, size_t size);
		bool pending_data(SOCKET socket);
		void disconnect(SOCKET socket);
		void frame() override;

	protected:
		virtual void handle(SOCKET socket, const std::string& data) = 0;
		virtual void on_disconnect(SOCKET socket);

		void send(const std::string& data);
		void close_connection();

	private:
		struct in_packet
		{
			SOCKET socket{};
			std::string data{};
		};

		using in_queue = std::queue<in_packet>;
		using socket_queue_map = std::unordered_map<SOCKET, stream_queue>;

		utils::concurrency::container<in_queue> in_queue_;
		utils::concurrency::container<socket_queue_map> out_queues_;
		utils::concurrency::container<std::unordered_set<SOCKET>> closing_sockets_;
		std::mutex handler_mutex_{};
		SOCKET current_socket_{INVALID_SOCKET};
	};
}
