#include <std_include.hpp>
#include "../dw_include.hpp"

#include "tcp_server.hpp"

namespace demonware
{
	void tcp_server::handle_input(const SOCKET socket, const char* buf, const size_t size)
	{
		in_queue_.access([&](in_queue& queue)
		{
			queue.push({socket, std::string{buf, size}});
		});
	}

	size_t tcp_server::handle_output(const SOCKET socket, char* buf, const size_t size)
	{
		const auto result = out_queues_.access<size_t>([&](socket_queue_map& queues)
		{
			const auto entry = queues.find(socket);
			if (entry == queues.end())
			{
				return size_t{0};
			}

			auto& queue = entry->second;
			for (size_t i = 0; i < size; ++i)
			{
				if (queue.empty())
				{
					queues.erase(entry);
					return i;
				}

				buf[i] = queue.front();
				queue.pop();
			}

			return size;
		});

		return result;
	}

	bool tcp_server::pending_data(const SOCKET socket)
	{
		const auto has_output = out_queues_.access<bool>([socket](const socket_queue_map& queues)
		{
			const auto entry = queues.find(socket);
			return entry != queues.end() && !entry->second.empty();
		});

		return has_output || closing_sockets_.access<bool>([socket](const std::unordered_set<SOCKET>& sockets)
		{
			return sockets.contains(socket);
		});
	}

	void tcp_server::disconnect(const SOCKET socket)
	{
		std::lock_guard handler_lock{handler_mutex_};
		in_queue_.access([socket](in_queue& queue)
		{
			in_queue remaining{};
			while (!queue.empty())
			{
				if (queue.front().socket != socket)
				{
					remaining.push(std::move(queue.front()));
				}

				queue.pop();
			}

			queue = std::move(remaining);
		});

		on_disconnect(socket);

		out_queues_.access([socket](socket_queue_map& queues)
		{
			queues.erase(socket);
		});

		closing_sockets_.access([socket](std::unordered_set<SOCKET>& sockets)
		{
			sockets.erase(socket);
		});
	}

	void tcp_server::on_disconnect(const SOCKET /*socket*/)
	{
	}

	void tcp_server::frame()
	{
		while (true)
		{
			std::lock_guard handler_lock{handler_mutex_};
			in_packet packet{};
			const auto result = this->in_queue_.access<bool>([&](in_queue& queue)
			{
				if (queue.empty())
				{
					return false;
				}

				packet = std::move(queue.front());
				queue.pop();
				return true;
			});

			if (!result)
			{
				break;
			}

			current_socket_ = packet.socket;
			this->handle(packet.socket, packet.data);
			current_socket_ = INVALID_SOCKET;
		}
	}

	void tcp_server::send(const std::string& data)
	{
		if (current_socket_ == INVALID_SOCKET)
		{
			return;
		}

		out_queues_.access([&](socket_queue_map& queues)
		{
			auto& queue = queues[current_socket_];
			for (const auto& val : data)
			{
				queue.push(val);
			}
		});
	}

	void tcp_server::close_connection()
	{
		if (current_socket_ == INVALID_SOCKET)
		{
			return;
		}

		closing_sockets_.access([this](std::unordered_set<SOCKET>& sockets)
		{
			sockets.insert(current_socket_);
		});
	}
}
