#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include <utils/hook.hpp>
#include <utils/thread.hpp>

#include "game/game.hpp"
#include "game/demonware/servers/lobby_server.hpp"
#include "game/demonware/servers/auth3_server.hpp"
#include "game/demonware/servers/stun_server.hpp"
#include "game/demonware/servers/umbrella_server.hpp"
#include "game/demonware/servers/uno_server.hpp"
#include "game/demonware/servers/glutton_server.hpp"
#include "game/demonware/server_registry.hpp"

#include "master_server.hpp"
#include "console/console.hpp"

#define TCP_BLOCKING true
#define UDP_BLOCKING false

namespace demonware
{
	namespace
	{
		std::atomic_bool exit_server{false};
		std::thread server_thread{};
		utils::concurrency::container<std::unordered_map<SOCKET, bool>> blocking_sockets{};
		utils::concurrency::container<std::unordered_map<SOCKET, tcp_server*>> socket_map{};
		server_registry<tcp_server> tcp_servers{};
		server_registry<udp_server> udp_servers{};
		std::unordered_map<void*, void*> original_imports{};
		tcp_server* find_server(const SOCKET socket)
		{
			return socket_map.access<tcp_server*>([&](const std::unordered_map<SOCKET, tcp_server*>& map) -> tcp_server*
			{
				const auto entry = map.find(socket);
				if (entry == map.end())
				{
					return nullptr;
				}

				return entry->second;
			});
		}

		bool socket_link(const SOCKET socket, const uint32_t address)
		{
			auto* server = tcp_servers.find(address);
			if (!server)
			{
				return false;
			}

			socket_map.access([&](std::unordered_map<SOCKET, tcp_server*>& map)
			{
				map[socket] = server;
			});

			return true;
		}

		void socket_unlink(const SOCKET socket)
		{
			socket_map.access([&](std::unordered_map<SOCKET, tcp_server*>& map)
			{
				const auto entry = map.find(socket);
				if (entry != map.end())
				{
					map.erase(entry);
				}
			});
		}

		bool is_socket_blocking(const SOCKET socket, const bool def)
		{
			return blocking_sockets.access<bool>([&](std::unordered_map<SOCKET, bool>& map)
			{
				const auto entry = map.find(socket);
				if (entry == map.end())
				{
					return def;
				}

				return entry->second;
			});
		}

		void remove_blocking_socket(const SOCKET socket)
		{
			blocking_sockets.access([&](std::unordered_map<SOCKET, bool>& map)
			{
				const auto entry = map.find(socket);
				if (entry != map.end())
				{
					map.erase(entry);
				}
			});
		}

		void add_blocking_socket(const SOCKET socket, const bool block)
		{
			blocking_sockets.access([&](std::unordered_map<SOCKET, bool>& map)
			{
				map[socket] = block;
			});
		}

		void server_main()
		{
			exit_server = false;

			while (!exit_server)
			{
				tcp_servers.frame();
				udp_servers.frame();
				std::this_thread::sleep_for(50ms);
			}
		}

		namespace io
		{
			int getaddrinfo_stub(const char* name, const char* service,
			                     const addrinfo* hints, addrinfo** res)
			{
#ifndef NDEBUG
				printf("[ network ]: [getaddrinfo]: \"%s\" \"%s\"\n", name, service);
#endif

				base_server* server = tcp_servers.find(name);
				if (!server)
				{
					server = udp_servers.find(name);
				}

				if (!server)
				{
					return getaddrinfo(name, service, hints, res);
				}

				const auto address = utils::memory::get_allocator()->allocate<sockaddr>();
				const auto ai = utils::memory::get_allocator()->allocate<addrinfo>();

				auto in_addr = reinterpret_cast<sockaddr_in*>(address);
				in_addr->sin_addr.s_addr = server->get_address();
				in_addr->sin_family = AF_INET;

				ai->ai_family = AF_INET;
				ai->ai_socktype = SOCK_STREAM;
				ai->ai_addr = address;
				ai->ai_addrlen = sizeof(sockaddr);
				ai->ai_next = nullptr;
				ai->ai_flags = 0;
				ai->ai_protocol = 0;
				ai->ai_canonname = const_cast<char*>(name);

				*res = ai;

				return 0;
			}

			void freeaddrinfo_stub(addrinfo* ai)
			{
				if (!utils::memory::get_allocator()->find(ai))
				{
					return freeaddrinfo(ai);
				}

				utils::memory::get_allocator()->free(ai->ai_addr);
				utils::memory::get_allocator()->free(ai);
			}

			int getpeername_stub(const SOCKET s, sockaddr* addr, socklen_t* addrlen)
			{
				auto* server = find_server(s);

				if (server)
				{
					auto in_addr = reinterpret_cast<sockaddr_in*>(addr);
					in_addr->sin_addr.s_addr = server->get_address();
					in_addr->sin_family = AF_INET;
					*addrlen = sizeof(sockaddr);

					return 0;
				}

				return getpeername(s, addr, addrlen);
			}

			int getsockname_stub(const SOCKET s, sockaddr* addr, socklen_t* addrlen)
			{
				auto* server = find_server(s);

				if (server)
				{
					auto in_addr = reinterpret_cast<sockaddr_in*>(addr);
					in_addr->sin_addr.s_addr = server->get_address();
					in_addr->sin_family = AF_INET;
					*addrlen = sizeof(sockaddr);

					return 0;
				}

				return getsockname(s, addr, addrlen);
			}

			hostent* gethostbyname_stub(const char* name)
			{
#ifndef NDEBUG
				printf("[ network ]: [gethostbyname]: \"%s\"\n", name);
#endif

				base_server* server = tcp_servers.find(name);
				if (!server)
				{
					server = udp_servers.find(name);
				}

				uint32_t resolved_address{};
				if (!server)
				{
#pragma warning(push)
#pragma warning(disable: 4996)
					auto* result = gethostbyname(name);
#pragma warning(pop)
					if (result)
					{
						return result;
					}

					// Windows may not resolve the local hostname without an active network adapter.
					// bdNet requires at least one local address, so use loopback only for that lookup.
					char hostname[256]{};
					if (gethostname(hostname, static_cast<int>(sizeof(hostname))) == SOCKET_ERROR ||
						_stricmp(name, hostname) != 0)
					{
						return nullptr;
					}

					resolved_address = htonl(INADDR_LOOPBACK);
				}
				else
				{
					resolved_address = server->get_address();
				}

				static thread_local in_addr address{};
				address.s_addr = resolved_address;

				static thread_local in_addr* addr_list[2]{};
				addr_list[0] = &address;
				addr_list[1] = nullptr;

				static thread_local hostent host{};
				host.h_name = const_cast<char*>(name);
				host.h_aliases = nullptr;
				host.h_addrtype = AF_INET;
				host.h_length = sizeof(in_addr);
				host.h_addr_list = reinterpret_cast<char**>(addr_list);

				return &host;
			}

			int connect_stub(const SOCKET s, const struct sockaddr* addr, const int len)
			{
				if (len == sizeof(sockaddr_in))
				{
					const auto* in_addr = reinterpret_cast<const sockaddr_in*>(addr);
					if (socket_link(s, in_addr->sin_addr.s_addr)) return 0;
				}

				return connect(s, addr, len);
			}

			int closesocket_stub(const SOCKET s)
			{
				if (auto* server = find_server(s))
				{
					server->disconnect(s);
				}

				remove_blocking_socket(s);
				socket_unlink(s);

				return closesocket(s);
			}

			int send_stub(const SOCKET s, const char* buf, const int len, const int flags)
			{
				auto* server = find_server(s);

				if (server)
				{
					server->handle_input(s, buf, len);
					return len;
				}

				return send(s, buf, len, flags);
			}

			int recv_stub(const SOCKET s, char* buf, const int len, const int flags)
			{
				auto* server = find_server(s);

				if (server)
				{
					if (server->pending_data(s))
					{
						return static_cast<int>(server->handle_output(s, buf, len));
					}
					else
					{
						WSASetLastError(WSAEWOULDBLOCK);
						return -1;
					}
				}

				return recv(s, buf, len, flags);
			}

			int sendto_stub(const SOCKET s, const char* buf, const int len, const int flags, const sockaddr* to,
			                const int tolen)
			{
				const auto* in_addr = reinterpret_cast<const sockaddr_in*>(to);
				auto* server = udp_servers.find(in_addr->sin_addr.s_addr);

				if (server)
				{
					server->handle_input(buf, len, {s, to, tolen});
					return len;
				}

				return sendto(s, buf, len, flags, to, tolen);
			}

			int recvfrom_stub(const SOCKET s, char* buf, const int len, const int flags, struct sockaddr* from,
			                  int* fromlen)
			{
				const auto receive_packet = [&]()
				{
					const auto result = recvfrom(s, buf, len, flags, from, fromlen);
					if (result <= 0 || !from || !fromlen || *fromlen < sizeof(sockaddr_in)
						|| from->sa_family != AF_INET)
					{
						return result;
					}

					const auto* address = reinterpret_cast<const sockaddr_in*>(from);
					if (s == *game::ip_socket && master_server::handle_incoming_packet(
						buf, result, address->sin_addr.s_addr, address->sin_port))
					{
						WSASetLastError(WSAEWOULDBLOCK);
						return SOCKET_ERROR;
					}

					return result;
				};

				// Not supported yet
				if (is_socket_blocking(s, UDP_BLOCKING))
				{
					return receive_packet();
				}

				size_t result = 0;
				udp_servers.for_each([&](udp_server& server)
				{
					if (server.pending_data(s))
					{
						result = server.handle_output(
							s, buf, static_cast<size_t>(len), from, fromlen);
					}
				});

				if (result)
				{
					return static_cast<int>(result);
				}

				return receive_packet();
			}

			int WSAAPI wsa_recv_from_stub(const SOCKET s, LPWSABUF buffers, const DWORD buffer_count,
			                              LPDWORD bytes_received, LPDWORD flags, sockaddr* from, LPINT fromlen,
			                              LPWSAOVERLAPPED overlapped,
			                              LPWSAOVERLAPPED_COMPLETION_ROUTINE completion_routine)
			{
				if (!overlapped && buffers && buffer_count && from && fromlen && *fromlen >= sizeof(sockaddr_in))
				{
					size_t result = 0;
					udp_servers.for_each([&](udp_server& server)
					{
						if (!result && server.pending_data(s))
						{
							result = server.handle_output(s, buffers[0].buf, buffers[0].len, from, fromlen);
						}
					});

					if (result)
					{
						if (bytes_received) *bytes_received = static_cast<DWORD>(result);
						if (flags) *flags = 0;
						return 0;
					}
				}

				return WSARecvFrom(s, buffers, buffer_count, bytes_received, flags, from, fromlen, overlapped,
				                   completion_routine);
			}

			int select_stub(const int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds,
			                struct timeval* timeout)
			{
				if (exit_server)
				{
					return select(nfds, readfds, writefds, exceptfds, timeout);
				}

				auto result = 0;
				std::vector<SOCKET> read_sockets;
				std::vector<SOCKET> write_sockets;

				socket_map.access([&](std::unordered_map<SOCKET, tcp_server*>& sockets)
				{
					for (auto& s : sockets)
					{
						if (readfds)
						{
							if (FD_ISSET(s.first, readfds))
							{
								if (s.second->pending_data(s.first))
								{
									read_sockets.push_back(s.first);
									FD_CLR(s.first, readfds);
								}
							}
						}

						if (writefds)
						{
							if (FD_ISSET(s.first, writefds))
							{
								write_sockets.push_back(s.first);
								FD_CLR(s.first, writefds);
							}
						}

						if (exceptfds)
						{
							if (FD_ISSET(s.first, exceptfds))
							{
								FD_CLR(s.first, exceptfds);
							}
						}
					}
				});

				if ((!readfds || readfds->fd_count == 0) && (!writefds || writefds->fd_count == 0))
				{
					timeout->tv_sec = 0;
					timeout->tv_usec = 0;
				}

				result = select(nfds, readfds, writefds, exceptfds, timeout);
				if (result < 0) result = 0;

				for (const auto& socket : read_sockets)
				{
					if (readfds)
					{
						FD_SET(socket, readfds);
						result++;
					}
				}

				for (const auto& socket : write_sockets)
				{
					if (writefds)
					{
						FD_SET(socket, writefds);
						result++;
					}
				}

				return result;
			}

			int ioctlsocket_stub(const SOCKET s, const long cmd, u_long* argp)
			{
				if (static_cast<unsigned long>(cmd) == (FIONBIO))
				{
					add_blocking_socket(s, *argp == 0);
				}

				return ioctlsocket(s, cmd, argp);
			}
		}

		void register_hook(const std::string& process, void* stub, const bool required = true)
		{
			const utils::nt::library game_module{};

			std::optional<std::pair<void*, void*>> result{};
			if (!result) result = utils::hook::iat(game_module, "wsock32.dll", process, stub);
			if (!result) result = utils::hook::iat(game_module, "WS2_32.dll", process, stub);

			if (!result)
			{
				if (!required)
				{
					return;
				}

				throw std::runtime_error("Failed to hook: " + process);
			}

			original_imports[result->first] = result->second;
		}

		namespace bridge
		{
			struct listener
			{
				SOCKET socket{INVALID_SOCKET};
				std::uint16_t port{};
				tcp_server* server{};
			};

			struct connection
			{
				SOCKET socket{INVALID_SOCKET};
				tcp_server* server{};
			};

			using curl_easy_setopt_t = int(*)(void*, int, void*);

			std::mutex listener_mutex{};
			std::vector<listener> listeners{};
			bool winsock_started{};
			std::thread thread{};
			curl_easy_setopt_t curl_easy_setopt_original{};

			std::optional<std::uint16_t> get_port(tcp_server* server)
			{
				std::lock_guard lock{listener_mutex};
				for (const auto& entry : listeners)
				{
					if (entry.server == server)
					{
						return entry.port;
					}
				}

				if (!winsock_started)
				{
					WSADATA data{};
					if (WSAStartup(MAKEWORD(2, 2), &data))
					{
						return {};
					}

					winsock_started = true;
				}

				const auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
				if (socket == INVALID_SOCKET)
				{
					return {};
				}

				sockaddr_in address{};
				address.sin_family = AF_INET;
				address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
				auto address_length = static_cast<int>(sizeof(address));
				u_long non_blocking = 1;

				if (bind(socket, reinterpret_cast<sockaddr*>(&address), address_length) == SOCKET_ERROR
					|| listen(socket, SOMAXCONN) == SOCKET_ERROR
					|| getsockname(socket, reinterpret_cast<sockaddr*>(&address), &address_length) == SOCKET_ERROR
					|| ioctlsocket(socket, FIONBIO, &non_blocking) == SOCKET_ERROR)
				{
					closesocket(socket);
					return {};
				}

				const auto port = ntohs(address.sin_port);
				listeners.push_back({socket, port, server});
				return port;
			}

			std::optional<std::uint16_t> get_host_port(const std::string& host)
			{
				in_addr address{};
				auto* server = inet_pton(AF_INET, host.data(), &address) == 1
					? tcp_servers.find(address.s_addr)
					: tcp_servers.find(host);

				if (!server)
				{
					console::demonware("[DW]: [bridge]: passing through %s\n", host.data());
					return {};
				}

				const auto port = get_port(server);
				if (!port)
				{
					console::error("[DW]: [bridge]: failed to listen for %s (%d)\n", server->get_name().data(), WSAGetLastError());
					return {};
				}

				console::demonware("[DW]: [bridge]: %s -> 127.0.0.1:%u\n", host.data(), *port);
				return port;
			}

			std::optional<std::string> rewrite_url(const std::string_view url)
			{
				const auto scheme_end = url.find("://");
				if (scheme_end == std::string_view::npos)
				{
					return {};
				}

				const auto host_begin = scheme_end + 3;
				const auto path_begin = std::min(url.find('/', host_begin), url.size());
				auto host = url.substr(host_begin, path_begin - host_begin);
				host = host.substr(0, host.find(':'));

				const auto port = get_host_port(std::string{host});
				if (!port)
				{
					return {};
				}

				return "http://127.0.0.1:" + std::to_string(*port) + std::string{url.substr(path_begin)};
			}

			using winhttp_connect_t = void*(WINAPI*)(void*, const wchar_t*, std::uint16_t, DWORD);
			using winhttp_open_request_t = void*(WINAPI*)(void*, const wchar_t*, const wchar_t*, const wchar_t*,
				const wchar_t*, const wchar_t**, DWORD);
			using winhttp_close_handle_t = BOOL(WINAPI*)(void*);

			constexpr DWORD winhttp_flag_secure = 0x00800000;

			winhttp_connect_t winhttp_connect_original{};
			winhttp_open_request_t winhttp_open_request_original{};
			winhttp_close_handle_t winhttp_close_handle_original{};
			utils::concurrency::container<std::unordered_set<void*>> bridged_handles{};

			void* WINAPI winhttp_connect_stub(void* session, const wchar_t* server_name, const std::uint16_t server_port,
				const DWORD reserved)
			{
				if (server_name)
				{
					std::string host{};
					for (const auto* character = server_name; *character; ++character)
					{
						host.push_back(static_cast<char>(*character));
					}

					if (const auto port = get_host_port(host))
					{
						auto* connection = winhttp_connect_original(session, L"127.0.0.1", *port, reserved);
						if (connection)
						{
							bridged_handles.access([connection](std::unordered_set<void*>& handles)
							{
								handles.insert(connection);
							});
						}

						return connection;
					}
				}

				return winhttp_connect_original(session, server_name, server_port, reserved);
			}

			void* WINAPI winhttp_open_request_stub(void* connection, const wchar_t* verb, const wchar_t* object_name,
				const wchar_t* version, const wchar_t* referrer, const wchar_t** accept_types, DWORD flags)
			{
				const auto bridged = bridged_handles.access<bool>([connection](const std::unordered_set<void*>& handles)
				{
					return handles.contains(connection);
				});

				if (!bridged)
				{
					return winhttp_open_request_original(connection, verb, object_name, version, referrer, accept_types, flags);
				}

				auto* request = winhttp_open_request_original(connection, verb, object_name, version, referrer, accept_types,
					flags & ~winhttp_flag_secure);
				if (request)
				{
					bridged_handles.access([request](std::unordered_set<void*>& handles)
					{
						handles.insert(request);
					});
				}

				return request;
			}

			HRESULT verify_server_certificate_stub(void* request, void* security_information)
			{
				const auto bridged = bridged_handles.access<bool>([request](const std::unordered_set<void*>& handles)
				{
					return handles.contains(request);
				});

				if (bridged)
				{
					return S_OK;
				}

				return utils::hook::invoke<HRESULT>(0x14088DFF0_ms, request, security_information);
			}

			BOOL WINAPI winhttp_close_handle_stub(void* handle)
			{
				bridged_handles.access([handle](std::unordered_set<void*>& handles)
				{
					handles.erase(handle);
				});

				return winhttp_close_handle_original(handle);
			}

			int curl_easy_setopt_stub(void* handle, const int option, void* parameter)
			{
				constexpr auto curlopt_url = 10002;
				if (option == curlopt_url && parameter)
				{
					if (const auto url = rewrite_url(static_cast<const char*>(parameter)))
					{
						return curl_easy_setopt_original(handle, option, const_cast<char*>(url->data()));
					}
				}

				return curl_easy_setopt_original(handle, option, parameter);
			}

			bool send_all(const SOCKET socket, const char* data, const size_t length)
			{
				size_t sent = 0;
				while (sent < length && !exit_server)
				{
					const auto result = send(socket, data + sent, static_cast<int>(length - sent), 0);
					if (result > 0)
					{
						sent += static_cast<size_t>(result);
					}
					else if (result == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
					{
						std::this_thread::sleep_for(1ms);
					}
					else
					{
						return false;
					}
				}

				return sent == length;
			}

			void close_connection(const connection& entry)
			{
				entry.server->disconnect(entry.socket);
				closesocket(entry.socket);
			}

			void run()
			{
				std::vector<connection> connections{};
				char buffer[0x4000];

				while (!exit_server)
				{
					std::vector<listener> current_listeners{};
					{
						std::lock_guard lock{listener_mutex};
						current_listeners = listeners;
					}

					fd_set read_set{};
					FD_ZERO(&read_set);
					for (const auto& entry : current_listeners)
					{
						FD_SET(entry.socket, &read_set);
					}

					for (const auto& entry : connections)
					{
						FD_SET(entry.socket, &read_set);
					}

					timeval timeout{0, 10000};
					if (!read_set.fd_count || select(0, &read_set, nullptr, nullptr, &timeout) == SOCKET_ERROR)
					{
						std::this_thread::sleep_for(10ms);
						FD_ZERO(&read_set);
					}

					for (const auto& entry : current_listeners)
					{
						if (!FD_ISSET(entry.socket, &read_set))
						{
							continue;
						}

						SOCKET socket{};
						while ((socket = accept(entry.socket, nullptr, nullptr)) != INVALID_SOCKET)
						{
							u_long non_blocking = 1;
							ioctlsocket(socket, FIONBIO, &non_blocking);
							connections.push_back({socket, entry.server});
						}
					}

					for (auto entry = connections.begin(); entry != connections.end();)
					{
						auto open = true;
						if (FD_ISSET(entry->socket, &read_set))
						{
							const auto received = recv(entry->socket, buffer, static_cast<int>(sizeof(buffer)), 0);
							if (received > 0)
							{
								console::demonware("[DW]: [bridge]: received %d bytes for %s\n", received,
									entry->server->get_name().data());
								entry->server->handle_input(entry->socket, buffer, static_cast<size_t>(received));
							}
							else if (received == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
							{
								open = false;
							}
						}

						while (open && entry->server->pending_data(entry->socket))
						{
							const auto length = entry->server->handle_output(entry->socket, buffer, sizeof(buffer));
							open = length && send_all(entry->socket, buffer, length);
						}

						if (open)
						{
							++entry;
							continue;
						}

						close_connection(*entry);
						entry = connections.erase(entry);
					}
				}

				for (const auto& entry : connections)
				{
					close_connection(entry);
				}

				std::lock_guard lock{listener_mutex};
				for (const auto& entry : listeners)
				{
					closesocket(entry.socket);
				}

				listeners.clear();
				if (winsock_started)
				{
					WSACleanup();
					winsock_started = false;
				}
			}

			template <typename T>
			void hook_import(const size_t address, T& original, T stub)
			{
				auto* const iat_entry = reinterpret_cast<void**>(address);
				original = reinterpret_cast<T>(*iat_entry);
				original_imports[iat_entry] = *iat_entry;
				utils::hook::set<void*>(iat_entry, reinterpret_cast<void*>(stub));
			}

			void install()
			{
				hook_import(0x140B21090_ms, curl_easy_setopt_original, curl_easy_setopt_stub);
				hook_import(0x140B20E90_ms, winhttp_connect_original, winhttp_connect_stub);
				hook_import(0x140B20E78_ms, winhttp_open_request_original, winhttp_open_request_stub);
				hook_import(0x140B20EC0_ms, winhttp_close_handle_original, winhttp_close_handle_stub);
				utils::hook::call(0x140A4D434_ms, verify_server_certificate_stub);
				utils::hook::set<std::uint32_t>(0x140A4E17F_ms, 0x10000000);
			}
		}

		BOOL WINAPI internet_get_connected_state_stub(LPDWORD flags, DWORD reserved)
		{
			return TRUE;
		}

		bool online_data_qos_ready_stub()
		{
			return true;
		}
	}

	class component final : public generic_component
	{
	public:
		component()
		{
			udp_servers.create<stun_server>("ww2-stun.us.demonware.net");
			udp_servers.create<stun_server>("ww2-stun.eu.demonware.net");
			udp_servers.create<stun_server>("ww2-stun.jp.demonware.net");
			udp_servers.create<stun_server>("ww2-stun.au.demonware.net");

			udp_servers.create<stun_server>("stun.us.demonware.net");
			udp_servers.create<stun_server>("stun.eu.demonware.net");
			udp_servers.create<stun_server>("stun.jp.demonware.net");
			udp_servers.create<stun_server>("stun.au.demonware.net");

			tcp_servers.create<auth3_server>("ww2-pc-auth3.prod.demonware.net");
			tcp_servers.create<lobby_server>("ww2-pc-lobby.prod.demonware.net");
			tcp_servers.create<umbrella_server>("prod.umbrella.demonware.net");
			tcp_servers.create<uno_server>("prod.uno.demonware.net");
			tcp_servers.create<glutton_server>("pipes-prod-glutton.public.aws.demonware.net");

			tcp_servers.alias("auth3.prod.demonware.net", "ww2-pc-auth3.prod.demonware.net");
			tcp_servers.alias("lsg.2810.prod.demonware.net", "ww2-pc-lobby.prod.demonware.net");
		}

		void post_load() override
		{
			register_hook("send", io::send_stub);
			register_hook("recv", io::recv_stub);
			register_hook("sendto", io::sendto_stub);
			register_hook("recvfrom", io::recvfrom_stub);
			register_hook("connect", io::connect_stub);
			register_hook("select", io::select_stub);
			register_hook("closesocket", io::closesocket_stub);
			register_hook("ioctlsocket", io::ioctlsocket_stub);
			register_hook("getaddrinfo", io::getaddrinfo_stub);
			register_hook("freeaddrinfo", io::freeaddrinfo_stub);
			const auto steam_only_import = !game::environment::is_store_native();
			register_hook("getpeername", io::getpeername_stub, steam_only_import);
			register_hook("getsockname", io::getsockname_stub, steam_only_import);

			if (game::environment::is_store_native())
			{
				register_hook("WSARecvFrom", io::wsa_recv_from_stub);
				bridge::install();
			}

			if (game::environment::uses_multiplayer_binary())
			{
				register_hook("gethostbyname", io::gethostbyname_stub);

				// Allow offline play
				auto* internet_state_import = utils::nt::library{}.get_iat_entry("wininet.dll", "InternetGetConnectedState");
				if (internet_state_import) utils::hook::set(internet_state_import, internet_get_connected_state_stub);
			}
		}

		void post_unpack() override
		{
			server_thread = utils::thread::create_named_thread("Demonware", server_main);
			if (game::environment::is_store_native())
			{
				bridge::thread = utils::thread::create_named_thread("Demonware Bridge", bridge::run);
			}

			// Skip bdAuth::validateResponseSignature
			utils::hook::set(game::select(0xA7ABA0, 0xAA2D00, 0x7B3FF0), 0xC301B0); // bdRSAKey::importKey
			utils::hook::set(game::select(0xA7ACC0, 0xAA2E20, 0x7B4110), 0xC300000001B8); // bdRSAKey::verifySignatureSHA256

			if (!game::environment::is_store_native())
			{
				utils::hook::set<uint8_t>(game::select(0xA249F7, 0x0, 0x77FC87) + 3, 0x0); // CURLOPT_SSL_VERIFYPEER
				utils::hook::set<uint8_t>(game::select(0xA249E0, 0x0, 0x77FC70) + 3, 0xAF); // CURLOPT_SSL_VERIFYHOST
			}

			if (game::environment::uses_multiplayer_binary())
			{
				utils::hook::jump(game::select(0x200C90, 0x1B0380), online_data_qos_ready_stub);
			}

			if (game::environment::is_store_native())
			{
				return;
			}

			utils::hook::set<uint8_t>(game::select(0xC62D0C, 0xC7783C, 0x96FD8C) + 4, 0x0); // HTTPS -> HTTP

			utils::hook::copy_string(game::select(0xC62F50, 0xC77A80, 0x96FA30), "http://prod.umbrella.demonware.net");
			utils::hook::copy_string(game::select(0xC62F28, 0xC77A58, 0x96FA08), "http://cert.umbrella.demonware.net");
			utils::hook::copy_string(game::select(0xC62F00, 0xC77A30, 0x96F9E0), "http://dev.umbrella.demonware.net");
			utils::hook::copy_string(game::select(0xB4A998, 0xB512D8, 0x861958),
				"http://prod.umbrella.demonware.net/v1.0/");

			utils::hook::copy_string(game::select(0xB4AA88, 0xB51308, 0x861A48), "http://prod.uno.demonware.net/v1.0/");
			if (game::environment::uses_multiplayer_binary())
			{
				utils::hook::copy_string(game::select(0xB4AB98, 0xB51330),
					"http://pipes-prod-glutton.public.aws.demonware.net/v1.0");
			}

			utils::hook::copy_string(game::select(0xC63F90, 0xC78AC0, 0x9705D0), "http://%s:%d/auth/");
		}

		void pre_destroy() override
		{
			exit_server = true;
			if (server_thread.joinable())
			{
				server_thread.join();
			}

			if (bridge::thread.joinable())
			{
				bridge::thread.join();
			}

			for (const auto& import : original_imports)
			{
				utils::hook::set(import.first, import.second);
			}
		}
	};
}

REGISTER_COMPONENT(demonware::component)
