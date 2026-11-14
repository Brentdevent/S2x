#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "upnp.hpp"
#include "upnp/protocol.hpp"
#include "command.hpp"
#include "console/console.hpp"
#include "party.hpp"
#include "scheduler.hpp"

#include "game/party_invite.hpp"
#include "game/types/upnp.hpp"
#include "game/upnp_policy.hpp"

#include <utils/hook.hpp>

#include <charconv>

namespace upnp
{
	namespace
	{
		using namespace game::native_upnp;
		using clock = std::chrono::steady_clock;

		constexpr auto renew_interval = 120s;
		constexpr auto operation_timeout = 30s;

		// Native bdUPnP constructor/start/pump/shutdown/destructor, verified in
		// the MP dump. No Demonware socket or stock UPnP configuration is changed.
		const game::symbol<void*(upnp_client*)> construct{0xA97B70};
		const game::symbol<void*(upnp_config*)> construct_config{0xA988E0};
		const game::symbol<void(upnp_client*)> pump{0xA98380};
		const game::symbol<void(upnp_client*, int)> stop{0xA98890};
		const game::symbol<void(upnp_device*)> retry_mapping{0xA9A4B0};
		const game::symbol<void(upnp_device*)> select_port{0xA9A410};
		const game::symbol<bool(upnp_device*, int*)> soap_error{0xA99420};
		const game::symbol<bool(upnp_device*)> connect_http{0xA98E90};
		const game::symbol<bool(upnp_device*, bool*)> send_http{0xA9A360};
		const game::symbol<bool(upnp_device*)> receive_http{0xA9A2B0};
		const game::symbol<bool(upnp_device*, bool*)> read_service_type{0xA99300};
		const game::symbol<void(void*)> close_http{0xA7B690};
		const game::symbol<bool(std::int32_t*, bool, unsigned, unsigned)> create_socket{0xA7B7B0};
		const game::symbol<bool(const bd_sockaddr*, char*, std::size_t)> address_string{0xA75B90};
		const game::symbol<bd_sockaddr*(bd_sockaddr*, const char*)> construct_address{0xA75810};

		utils::hook::detour start_hook;
		utils::hook::detour destroy_hook;
		utils::hook::detour soap_hook;
		utils::hook::detour receive_hook;
		utils::hook::detour http_success_hook;
		utils::hook::detour discovery_hook;

		std::recursive_mutex mutex;
		const game::dvar_t* enabled{};
		upnp_client* native_owner{};
		std::vector<bd_sockaddr> latest_addresses;
		std::unordered_set<std::uint32_t> tried_interfaces;
		std::uint64_t network_generation{};
		bool shutting_down{};

		enum class lookup_state
		{
			none,
			refreshing,
			sending,
			receiving,
		};

		struct mapping
		{
			upnp_client client{};
			std::vector<bd_sockaddr> storage;
			bd_address_array addresses{};
			bd_sockaddr gateway{};
			std::string endpoint;
			std::string action;
			SOCKET socket{INVALID_SOCKET};
			std::uint32_t bound_address{};
			std::uint32_t discovery_address{};
			std::uint16_t port{};
			std::uint16_t mapped_port{};
			std::uint64_t generation{};
			clock::time_point deadline{};
			clock::time_point renew_at{};
			clock::time_point lease_expires_at{};
			clock::time_point add_sent_at{};
			lookup_state lookup{};
			bool refreshed{};
			bool renewing{};
			bool replacing{};
			std::optional<policy::cleanup> cleanup;
		};

		std::unique_ptr<mapping> active;
		clock::time_point retry_at{};
		std::string last_error;

		void report_failure(const std::string& reason)
		{
			if (last_error != reason)
			{
				console::warn("[upnp] %s Automatic Custom Match reachability is unavailable.\n", reason.c_str());
				last_error = reason;
			}
		}

		// Only our object's SOAP request is adapted. A99650 still builds the native
		// envelope/HTTP request. In particular, never patch bdNet's mapping.
		int soap_stub(upnp_device* device, const char* action, const char* body)
		{
			const std::lock_guard lock(mutex);

			if (!active || device != &active->client.device)
			{
				return soap_hook.invoke<int>(device, action, body);
			}

			// A99E90 incorrectly uses +472 after a conflict changes +474. Also
			// bound orphaned mappings after a crash with a renewable finite lease.
			active->action = action;

			const auto adapted = policy::soap_body(action, body, device->external_port);
			const auto result = soap_hook.invoke<int>(device, action, adapted.c_str());

			if (result <= 0 || device->request_size >= sizeof(device->request))
			{
				return result;
			}

			// The native receiver finishes at EOF. Ask the router to close now,
			// so ownership lookup can finish within the quit cleanup budget.
			std::string request{device->request, device->request_size};
			const auto headers_end = request.find("\r\n\r\n");
			constexpr std::string_view connection = "Connection: close\r\n";

			if (headers_end == std::string::npos || request.size() + connection.size() >= sizeof(device->request))
			{
				return result;
			}

			request.insert(headers_end + 2, connection);
			std::memcpy(device->request, request.c_str(), request.size() + 1);
			device->request_size = static_cast<std::uint32_t>(request.size());
			return static_cast<int>(device->request_size);
		}

		bool create_socket_stub(std::int32_t* socket, const bool blocking,
			const unsigned send_size, const unsigned receive_size)
		{
			const std::lock_guard lock(mutex);

			if (!create_socket(socket, blocking, send_size, receive_size))
			{
				return false;
			}

			if (!active || static_cast<void*>(socket) != active->client.device.socket)
			{
				return true;
			}

			// A98E90 creates each HTTP socket here, before connect. Binding also
			// keeps description, Add, ownership lookup and Delete on this NIC.
			sockaddr_in local{};
			local.sin_family = AF_INET;
			local.sin_addr.s_addr = active->discovery_address;

			if (!local.sin_addr.s_addr || bind(static_cast<SOCKET>(*socket),
				reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR)
			{
				console::warn("[upnp] Cannot bind HTTP socket to discovery interface (WinSock %d).\n", WSAGetLastError());
				close_http(socket);
				return false;
			}

			return true;
		}

		bool discovery_stub(upnp_client* client)
		{
			const std::lock_guard lock(mutex);

			if (!active || client != &active->client)
			{
				return discovery_hook.invoke<bool>(client);
			}

			// Probe one interface per native client. A98380 accepts the first
			// reply, so broadcasting on every NIC lets a failed router win again.
			in_addr interface_address{};
			interface_address.s_addr = active->discovery_address;
			const auto socket = static_cast<SOCKET>(client->discovery_socket.handle);

			if (!interface_address.s_addr || setsockopt(socket, IPPROTO_IP, IP_MULTICAST_IF,
				reinterpret_cast<const char*>(&interface_address), sizeof(interface_address)) == SOCKET_ERROR)
			{
				console::warn("[upnp] Cannot select discovery interface (WinSock %d).\n", WSAGetLastError());
				return false;
			}

			if (!client->discovery_attempts)
			{
				char address[INET_ADDRSTRLEN]{};
				InetNtopA(AF_INET, &interface_address, address, sizeof(address));
				console::info("[upnp] Discovering routers on %s.\n", address);
			}

			return discovery_hook.invoke<bool>(client);
		}

		std::vector<bd_sockaddr> local_addresses()
		{
			// bdNet's startup list can omit adapters or become stale after a VPN
			// reconnect. Enumerate Windows for each new attempt, not during cleanup.
			ULONG size = 15 * 1024;
			std::vector<std::byte> buffer(size);
			ULONG result = ERROR_BUFFER_OVERFLOW;
			constexpr auto flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;

			for (unsigned attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt)
			{
				buffer.resize(size);
				result = GetAdaptersAddresses(AF_INET, flags, nullptr,
					reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
			}

			if (result == ERROR_NO_DATA)
			{
				return {};
			}

			if (result != NO_ERROR)
			{
				return latest_addresses;
			}

			std::vector<bd_sockaddr> addresses;
			std::unordered_set<std::uint32_t> seen;

			for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter; adapter = adapter->Next)
			{
				if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
				{
					continue;
				}

				for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next)
				{
					if (!unicast->Address.lpSockaddr || unicast->Address.iSockaddrLength < static_cast<int>(sizeof(sockaddr_in))
						|| unicast->Address.lpSockaddr->sa_family != AF_INET
						|| (unicast->DadState != IpDadStatePreferred && unicast->DadState != IpDadStateDeprecated))
					{
						continue;
					}

					const auto* local = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
					const auto host_address = ntohl(local->sin_addr.s_addr);

					if (!host_address || (host_address >> 24) == 127 || host_address >= 0xE0000000
						|| !seen.insert(local->sin_addr.s_addr).second)
					{
						continue;
					}

					char address[INET_ADDRSTRLEN]{};

					if (InetNtopA(AF_INET, &local->sin_addr, address, sizeof(address)))
					{
						construct_address(&addresses.emplace_back(), address);
					}
				}
			}

			return addresses;
		}

		std::uint32_t preferred_address()
		{
			// Ask for the default unicast route, independent of the SSDP route.
			MIB_IPFORWARDROW route{};
			ULONG size{};

			if (GetBestRoute(0, 0, &route) != NO_ERROR || route.dwForwardDest || route.dwForwardMask
				|| GetIpAddrTable(nullptr, &size, false) != ERROR_INSUFFICIENT_BUFFER)
			{
				return 0;
			}

			std::vector<std::byte> buffer(size);
			const auto addresses = reinterpret_cast<MIB_IPADDRTABLE*>(buffer.data());

			if (GetIpAddrTable(addresses, &size, false) == NO_ERROR)
			{
				for (DWORD i = 0; i < addresses->dwNumEntries; ++i)
				{
					if (addresses->table[i].dwIndex == route.dwForwardIfIndex)
					{
						return addresses->table[i].dwAddr;
					}
				}
			}

			return 0;
		}

		std::uint32_t next_interface(const std::vector<bd_sockaddr>& addresses,
			const std::uint32_t bound_address, const std::uint32_t preferred)
		{
			std::uint32_t fallback{};

			for (const auto& local : addresses)
			{
				char address[32]{};
				in_addr ipv4{};
				address_string(&local, address, sizeof(address));

				if (InetPtonA(AF_INET, address, &ipv4) != 1 || !ipv4.s_addr
					|| (bound_address && ipv4.s_addr != bound_address) || tried_interfaces.contains(ipv4.s_addr))
				{
					continue;
				}

				if (ipv4.s_addr == preferred)
				{
					return ipv4.s_addr;
				}

				if (!fallback)
				{
					fallback = ipv4.s_addr;
				}
			}

			return fallback;
		}

		bool start_stub(upnp_client* client, const std::uint16_t port,
			bd_address_array* addresses, upnp_config* config)
		{
			const std::lock_guard lock(mutex);

			// Own a copy: bdNet can replace/free its address array on restart.
			native_owner = client;
			latest_addresses.clear();

			if (addresses && addresses->data && addresses->count <= 64)
			{
				latest_addresses.assign(addresses->data, addresses->data + addresses->count);
			}

			++network_generation;
			tried_interfaces.clear();
			return start_hook.invoke<bool>(client, port, addresses, config);
		}

		void pump_client(mapping& value)
		{
			auto& device = value.client.device;

			if (device.mapping_result == 3 && clock::now() >= value.lease_expires_at)
			{
				device.mapping_result = 0;
				value.endpoint.clear();
				value.renewing = false;

				// Delete has been built but not sent yet. A pause here must not
				// authorize deleting a port that has since expired/reassigned.
				if (device.state == 11)
				{
					stop(&value.client, 1);
					return;
				}
			}

			const auto state = device.state;
			const auto sent_at = clock::now();
			pump(&value.client);

			if (device.state == 1)
			{
				// A98380 selected the description address. A98F60 can later
				// replace it with a different host from an absolute controlURL.
				std::memcpy(&value.gateway, device.router_address, sizeof(value.gateway));
			}

			if (state == 8 && device.state == 9)
			{
				// A99F00 advances only after send succeeds. Construction can be
				// much earlier if the process pauses while the request is unsent.
				value.add_sent_at = sent_at;
			}

			if (state == 9 && device.state == 10 && device.mapping_result == 3)
			{
				// A99F00 acknowledges Add in state 10, one pump before state 15.
				// Track it now, including when cleanup is waiting for this reply.
				value.mapped_port = device.external_port;
				value.lease_expires_at = value.add_sent_at + std::chrono::seconds{policy::lease_seconds};
				value.refreshed = false;
				value.renewing = false;
			}

			if (state == 12 && device.state == 13 && device.mapping_result == 2)
			{
				value.mapped_port = 0;
			}
		}

		bool receive_stub(upnp_device* device)
		{
			const std::lock_guard lock(mutex);
			const auto pending = receive_hook.invoke<bool>(device);

			if (pending || !active || device != &active->client.device)
			{
				return pending;
			}

			// A9A2B0 returns false at EOF, before A99F00 parses the response.
			// Decode description, external-IP and SOAP fault bodies too. Retain the
			// status line for http_success_stub and the XML for native SOAP parsing.
			std::string body;
			std::string response;

			if (device->response_size < sizeof(device->response))
			{
				const std::string_view input{device->response, device->response_size};

				if (protocol::read_http_body(input, body, true))
				{
					response = std::string{input.substr(0, input.find("\r\n"))}
						+ "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
				}
			}

			if (response.size() >= sizeof(device->response))
			{
				response.clear();
			}

			// An incomplete or malformed frame must not reach native strstr parsers.
			std::memcpy(device->response, response.c_str(), response.size() + 1);
			device->response_size = static_cast<std::uint32_t>(response.size());

			if (device->state == 3)
			{
				// A99980 stores the detected service only after A98F60 uses the old
				// selection to find its control URL, rejecting PPP-only routers.
				read_service_type(device, &device->wan_ip_connection);
			}

			return false;
		}

		bool http_success_stub(upnp_device* device)
		{
			const std::lock_guard lock(mutex);

			if (!active || device != &active->client.device)
			{
				return http_success_hook.invoke<bool>(device);
			}

			if (device->response_size >= sizeof(device->response))
			{
				return false;
			}

			// A98E60 searches the entire response for "200 OK". XML or a reason
			// phrase containing that text must not turn a SOAP fault into success.
			std::string body;
			return protocol::read_http_body({device->response, device->response_size}, body);
		}

		bool mapping_matches(upnp_device& device)
		{
			if (device.response_size >= sizeof(device.response))
			{
				return false;
			}

			const std::string_view response{device.response, device.response_size};
			std::string body;
			std::array<std::string, 3> fields{};

			if (!protocol::read_http_body(response, body) || !protocol::read_mapping_response(body, fields))
			{
				return false;
			}

			char internal[32]{};

			if (!address_string(&device.internal_address, internal, sizeof(internal)) || !*internal)
			{
				return false;
			}

			const auto& port = fields[0];
			unsigned number{};

			const auto parsed = std::from_chars(port.data(), port.data() + port.size(), number);
			return parsed.ec == std::errc{} && parsed.ptr == port.data() + port.size() && number == device.internal_port
				&& fields[1] == internal && fields[2] == "S2x Custom Match";
		}

		bool refresh_router(mapping& value)
		{
			value.lookup = lookup_state::none;
			value.client.device.mapping_result = 0;

			if (value.refreshed || !value.mapped_port || clock::now() >= value.lease_expires_at)
			{
				value.mapped_port = 0;
				return false;
			}

			value.refreshed = true;
			stop(&value.client, 1);

			// A97D00/A98220 retain the counter; this is a new discovery attempt.
			value.client.discovery_attempts = 0;

			upnp_config config{};
			construct_config(&config);
			// A99F00 state 7: run_mode 1 resolves the control URL without Add.
			// A98380/A758C0 filter by gateway IP, allowing its HTTP port to change.
			config.run_mode = 1;
			config.gateway = value.gateway;
			config.only_gateway = true;

			if (!start_hook.invoke<bool>(&value.client, value.port, &value.addresses, &config))
			{
				value.mapped_port = 0;
				return false;
			}

			value.lookup = lookup_state::refreshing;
			console::info("[upnp] Refreshing the router control address before ownership verification.\n");
			return true;
		}

		bool prepare_shutdown(mapping& value)
		{
			auto& device = value.client.device;

			if (value.lookup == lookup_state::refreshing)
			{
				if (clock::now() < value.deadline && clock::now() < value.lease_expires_at)
				{
					pump(&value.client);

					if (value.client.state != 6 && value.client.state != 7)
					{
						return false;
					}
				}

				char internal[32]{};
				in_addr address{};
				address_string(&device.internal_address, internal, sizeof(internal));

				if (value.client.state == 6 && device.state == 15
					&& InetPtonA(AF_INET, internal, &address) == 1 && address.s_addr == value.discovery_address)
				{
					device.external_port = value.mapped_port;
					device.mapping_result = 3;
					device.config.run_mode = value.client.config.run_mode = 0;
				}

				// Discovery does not renew the lease or prove ownership. The
				// lookup below must still verify the old port on the new URL.
				value.lookup = lookup_state::none;
			}

			if (device.mapping_result != 3 || clock::now() >= value.lease_expires_at
				|| ((value.lookup != lookup_state::none || value.refreshed) && clock::now() >= value.deadline))
			{
				device.mapping_result = 0;
				value.mapped_port = 0;
				value.lookup = lookup_state::none;
				return true;
			}

			if (value.lookup == lookup_state::none)
			{
				if (!value.refreshed)
				{
					value.deadline = clock::now() + operation_timeout;
				}

				// Reuse the native SOAP builder, nonblocking socket and timeouts.
				// A remembered lease alone cannot establish ownership after a
				// router restart, and 718 can mean a genuine foreign mapping.
				close_http(device.socket);
				const auto body = policy::mapping_key(device.external_port);
				soap_stub(&device, "GetSpecificPortMappingEntry", body.c_str());

				if (!connect_http(&device))
				{
					return !refresh_router(value);
				}

				value.lookup = lookup_state::sending;
			}

			if (value.lookup == lookup_state::sending)
			{
				bool sent{};

				if (!send_http(&device, &sent))
				{
					return !refresh_router(value);
				}

				if (sent)
				{
					value.lookup = lookup_state::receiving;
				}

				return false;
			}

			if (receive_http(&device))
			{
				return false;
			}

			if (!device.response_size)
			{
				return !refresh_router(value);
			}

			device.mapping_result = clock::now() < value.lease_expires_at && mapping_matches(device) ? 3 : 0;
			value.lookup = lookup_state::none;

			if (!device.mapping_result)
			{
				value.mapped_port = 0;
				console::warn("[upnp] Mapping ownership was not confirmed; leaving the router entry untouched.\n");
			}

			return true;
		}

		void destroy_mapping()
		{
			if (!active)
			{
				return;
			}

			stop(&active->client, 1);
			destroy_hook.invoke<void>(&active->client);
			active.reset();
		}

		void begin_stop()
		{
			if (!active || active->cleanup)
			{
				return;
			}

			active->endpoint.clear();

			if (active->lookup == lookup_state::none)
			{
				active->deadline = clock::now() + operation_timeout;
			}

			active->cleanup.emplace(clock::now() + operation_timeout);
		}

		void pump_stop()
		{
			if (!active)
			{
				return;
			}

			auto& value = *active;
			const auto action = value.cleanup->next(value.client.state, value.client.device.state, clock::now());

			// Native shutdown resets the device even if Delete could not reach
			// the old control URL. Retain the lease until one bounded refresh.
			if (action == policy::cleanup_action::complete && value.mapped_port && refresh_router(value))
			{
				return;
			}

			if (action == policy::cleanup_action::complete || action == policy::cleanup_action::force_cleanup)
			{
				if (action == policy::cleanup_action::force_cleanup)
				{
					console::warn("[upnp] Mapping cleanup timed out; its lease will expire.\n");
				}

				destroy_mapping();
				return;
			}

			if (value.lookup != lookup_state::none || (action == policy::cleanup_action::request_shutdown
				&& value.client.state != 5 && !value.client.shutdown_requested))
			{
				if (!prepare_shutdown(value))
				{
					return;
				}

				stop(&value.client, 0);
			}

			pump_client(value);
		}

		void drain()
		{
			begin_stop();

			if (!active)
			{
				return;
			}

			active->cleanup->shorten_deadline(clock::now() + 3s);

			while (active)
			{
				// Native HTTP timeouts exceed the quit budget. Leave time to
				// rediscover a changed control URL, verify ownership and Delete.
				active->client.config.connection_timeout = active->client.device.config.connection_timeout = 1.0f;

				// An in-flight Add may already be installed. Keep waiting for its
				// acknowledgement within the overall quit budget before cleanup.
				const auto state = active->client.device.state;

				if (state != 8 && state != 9)
				{
					active->client.config.response_timeout = active->client.device.config.response_timeout = 1.0f;
				}

				pump_stop();

				if (active)
				{
					Sleep(10);
				}
			}
		}

		void destroy_stub(upnp_client* client)
		{
			const std::lock_guard lock(mutex);

			if (client == native_owner)
			{
				// Release our native objects before bdNet/allocator teardown.
				drain();
				native_owner = nullptr;
				latest_addresses.clear();
				++network_generation;
			}

			destroy_hook.invoke<void>(client);
		}

		bool bound_socket(sockaddr_in& address)
		{
			const auto socket = *game::ip_socket;
			int length = sizeof(address);

			return socket != INVALID_SOCKET && socket != 0
				&& getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) == 0
				&& address.sin_family == AF_INET && address.sin_port != 0;
		}

		void start_mapping(const sockaddr_in& address)
		{
			auto addresses = local_addresses();
			const auto interface_address = next_interface(addresses, address.sin_addr.s_addr, preferred_address());

			if (!interface_address)
			{
				tried_interfaces.clear();
				retry_at = clock::now() + 60s;
				return;
			}

			tried_interfaces.insert(interface_address);
			active = std::make_unique<mapping>();
			auto& value = *active;
			value.storage = std::move(addresses);

			const auto address_count = static_cast<std::uint32_t>(value.storage.size());
			value.addresses = {value.storage.data(), address_count, address_count};
			value.socket = *game::ip_socket;
			value.bound_address = address.sin_addr.s_addr;
			value.discovery_address = interface_address;
			value.port = ntohs(address.sin_port);
			value.generation = network_generation;
			value.deadline = clock::now() + operation_timeout;
			construct(&value.client);

			upnp_config config{};
			construct_config(&config);

			// A98220 consumes/destroys this native by-value argument.
			if (!start_hook.invoke<bool>(&value.client, value.port, &value.addresses, &config))
			{
				report_failure("Native UPnP initialization failed.");
				destroy_mapping();
				retry_at = {};
			}
		}

		void renew_mapping(mapping& value)
		{
			// Keep the confirmed endpoint available while its existing lease is valid.
			value.renew_at = clock::now();
			value.deadline = value.renew_at + operation_timeout;
			value.refreshed = false;
			value.renewing = value.renew_at < value.lease_expires_at;
			value.client.state = 4;

			// Keep the successful mapping result for cleanup. A renew conflict
			// must not pick another port and forget the lease we already own.
			if (!value.renewing)
			{
				value.endpoint.clear();
				value.client.device.mapping_result = 0;
			}

			value.client.device.mapping_attempts = value.renewing ? value.client.device.config.mapping_retries : 0;
			retry_mapping(&value.client.device);
		}

		void pump_mapping(mapping& value)
		{
			if (value.replacing && value.client.state != 5 && !value.client.shutdown_requested)
			{
				if (!prepare_shutdown(value))
				{
					return;
				}

				if (!value.client.state || value.client.config.run_mode == 1)
				{
					// Failed rediscovery has no usable control URL for a new Add.
					value.replacing = false;
					begin_stop();
					return;
				}

				if (value.client.device.mapping_result == 3)
				{
					console::info("[upnp] Verified our UDP mapping; replacing the rejected renewal.\n");
					stop(&value.client, 0);
				}
				else
				{
					// Leave the conflicting/unknown entry alone. Resume native
					// port selection with a new candidate and normal retry limits.
					auto& device = value.client.device;
					close_http(device.socket);
					const auto previous_port = device.external_port;
					device.mapping_attempts = 1;
					select_port(&device);

					if (device.external_port == previous_port)
					{
						device.external_port = previous_port == 65535 ? 1 : previous_port + 1;
					}

					value.replacing = false;
					value.renewing = false;
					value.client.state = 4;
					value.deadline = clock::now() + operation_timeout;
					retry_mapping(&device);
				}
			}

			pump_client(value);

			if (value.replacing && value.client.state == 5 && value.client.device.state == 13)
			{
				// A99F00: Delete succeeded. Restart Add before native shutdown
				// resets the router/ports, retaining the same external port.
				value.renewing = false;
				value.replacing = false;
				value.deadline = clock::now() + operation_timeout;
				value.client.state = 4;
				value.client.device.mapping_result = 0;
				value.client.device.mapping_attempts = 0;
				retry_mapping(&value.client.device);
				return;
			}

			if (value.replacing)
			{
				value.replacing = value.client.state == 5 && clock::now() < value.deadline;
				return;
			}

			int error{};

			if (value.renewing && value.client.state == 7 && soap_error(&value.client.device, &error) && error == 718)
			{
				value.endpoint.clear();
				value.replacing = true;
				value.deadline = clock::now() + operation_timeout;
			}
		}

		bool is_hosting_party()
		{
			if (game::is_local_play())
			{
				return false;
			}

			// The social party stays connected while its members enter a game.
			// Its UDP mapping must survive even while native joins are suspended.
			auto* private_party = game::Party_GetPrivatePartyData();
			if (private_party && game::Party_AreWeHost(private_party)
				&& !party::get_current_session_id(1).empty())
			{
				return true;
			}

			party::session::connect_info info{};
			return party::get_invite_connect_info(info);
		}

		void tick()
		{
			const std::lock_guard lock(mutex);

			if (shutting_down)
			{
				return;
			}

			sockaddr_in address{};
			const auto hosting = enabled && enabled->current.enabled
				&& is_hosting_party() && bound_socket(address);

			if (active && (!hosting
				|| *game::ip_socket != active->socket || ntohs(address.sin_port) != active->port
				|| address.sin_addr.s_addr != active->bound_address || network_generation != active->generation))
			{
				tried_interfaces.clear();
				retry_at = {};
				begin_stop();
			}

			if (active && active->cleanup)
			{
				pump_stop();
				return;
			}

			if (!hosting)
			{
				tried_interfaces.clear();
				retry_at = {};
				return;
			}

			if (!active)
			{
				if (!native_owner || clock::now() < retry_at)
				{
					return;
				}

				start_mapping(address);

				if (!active)
				{
					return;
				}
			}

			auto& value = *active;

			if (value.client.state == 6 && !value.endpoint.empty() && clock::now() >= value.renew_at)
			{
				renew_mapping(value);
			}

			const auto device_state = value.client.device.state;
			pump_mapping(value);

			if (value.replacing || value.client.shutdown_requested)
			{
				return;
			}

			if (value.client.state == 6 && value.client.device.state == 15
				&& value.client.device.mapping_result == 3)
			{
				if (!value.endpoint.empty() && clock::now() < value.renew_at)
				{
					return;
				}

				char external[32]{};
				char internal[32]{};
				address_string(&value.client.device.external_address, external, sizeof(external));
				address_string(&value.client.device.internal_address, internal, sizeof(internal));

				in_addr mapped_internal{};
				const auto correct_interface = InetPtonA(AF_INET, internal, &mapped_internal) == 1
					&& mapped_internal.s_addr == value.discovery_address;

				// A99780 returns the original port. Use +474, the port actually
				// sent by AddPortMapping, including a successful conflict retry.
				const auto endpoint = std::string{external} + ":" + std::to_string(value.client.device.external_port);

				if (correct_interface && party::invite::valid_endpoint(endpoint))
				{
					value.endpoint = endpoint;
					value.renew_at = clock::now() + renew_interval;
					last_error.clear();
					console::info("[upnp] UDP %s -> %s:%u (Custom Match, %ds lease).\n",
						endpoint.c_str(), internal, value.port, policy::lease_seconds);
					return;
				}

				report_failure("Router returned a non-public address or a different bound interface.");
			}
			else if (value.client.state != 0 && value.client.state != 6 && value.client.state != 7
				&& clock::now() < value.deadline)
			{
				return;
			}
			else
			{
				int error{};
				const auto received_error = value.client.device.state == 16
					&& !value.action.empty() && soap_error(&value.client.device, &error);
				const auto stage = value.action.empty() ? "router discovery/description" : value.action;
				report_failure("Failed during " + stage + " (client " + std::to_string(value.client.state)
					+ ", device " + std::to_string(device_state) + " -> " + std::to_string(value.client.device.state)
					+ (received_error ? ", last SOAP error " + std::to_string(error) : "") + ").");
			}

			retry_at = {};
			begin_stop();
		}
	}

	std::string get_endpoint(const std::string_view session_id)
	{
		const std::lock_guard lock(mutex);

		party::session::connect_info info{};
		sockaddr_in address{};
		if (shutting_down || !enabled || !enabled->current.enabled || !active || active->cleanup
			|| active->generation != network_generation || clock::now() >= active->lease_expires_at
			|| !bound_socket(address) || *game::ip_socket != active->socket
			|| ntohs(address.sin_port) != active->port || address.sin_addr.s_addr != active->bound_address
			|| !party::get_invite_connect_info(info) || info.session.session_id != session_id)
		{
			return {};
		}

		return active->endpoint;
	}

	void shutdown()
	{
		const std::lock_guard lock(mutex);
		shutting_down = true;
		drain();
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated())
			{
				return;
			}

			scheduler::once([]
			{
				enabled = game::Dvar_RegisterBool("net_upnp", true, game::DVAR_FLAG_SAVED);
			}, scheduler::main);

			start_hook.create(0xA98220_g, start_stub);
			destroy_hook.create(0xA97BF0_g, destroy_stub);
			soap_hook.create(0xA99650_g, soap_stub);
			receive_hook.create(0xA9A2B0_g, receive_stub);
			http_success_hook.create(0xA98E60_g, http_success_stub);
			discovery_hook.create(0xA987B0_g, discovery_stub);
			// A98EB2 passes the socket, blocking flag and both buffer sizes.
			utils::hook::call(0xA98EB2_g, create_socket_stub);

			scheduler::loop(tick, scheduler::main, 50ms);

			command::add("upnp_status", []
			{
				const std::lock_guard lock(mutex);

				console::info("[upnp] %s\n", active && !active->endpoint.empty()
					? active->endpoint.c_str() : "No public Custom Match mapping.");

				if (!last_error.empty())
				{
					console::info("[upnp] Last failure: %s\n", last_error.c_str());
				}
			});
		}

		void pre_destroy() override
		{
			shutdown();
		}
	};
}

REGISTER_COMPONENT(upnp::component)
