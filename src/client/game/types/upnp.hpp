#pragma once

#include <cstddef>
#include <cstdint>

namespace game::native_upnp
{
	// MP dump RVAs are recorded beside each layout. Zombies shares this binary.
	// bdSockAddr's copy constructor (A75770) copies 132 bytes; its destructor
	// (A758B0) only poisons the first word. These values own no heap storage.
	struct bd_sockaddr
	{
		std::uint32_t storage[33];
	};

	static_assert(sizeof(bd_sockaddr) == 132);

	// A94BD0 / A96A40: pointer, capacity, count; element stride 132.
	struct bd_address_array
	{
		bd_sockaddr* data;
		std::uint32_t capacity;
		std::uint32_t count;
	};

	static_assert(sizeof(bd_address_array) == 16);
	static_assert(offsetof(bd_address_array, count) == 12);

	// A988E0 constructs this; A98220 consumes a by-value copy (passed by
	// pointer by the x64 ABI) and destroys its gateway at offset 36.
	struct upnp_config
	{
		float discovery_timeout;
		float response_timeout;
		float connection_timeout;
		std::uint32_t run_mode;
		std::uint32_t discovery_retries;
		std::uint32_t mapping_retries;
		std::uint32_t port_selection;
		std::uint32_t port_range;
		bool disabled;
		std::byte pad[3];
		bd_sockaddr gateway;
		bool only_gateway;
		std::byte tail[3];
	};

	static_assert(sizeof(upnp_config) == 172);
	static_assert(offsetof(upnp_config, gateway) == 36);
	static_assert(offsetof(upnp_config, only_gateway) == 168);

	// A98AE0 constructor, A98380 copies sizeof(device) = 0x2380.
	struct alignas(8) upnp_device
	{
		std::uint32_t state;
		bool shutdown_requested;
		std::byte pad[3];
		std::uint32_t shutdown_mode;
		std::uint32_t mapping_result;
		std::byte router_address[144];
		bd_sockaddr internal_address;
		std::byte socket[4];
		// A98AE0 defaults this to true; A98F60 uses it to find the control URL.
		bool wan_ip_connection;
		std::byte service_pad[3];
		upnp_config config;
		std::uint16_t internal_port;
		std::uint16_t external_port;
		bd_sockaddr external_address;
		char request[2048];
		std::uint32_t request_size;
		char response[6144];
		std::uint32_t response_size;
		std::byte paths_and_timers[208];
		std::uint32_t mapping_attempts;
		std::byte description[68];
	};

	static_assert(sizeof(upnp_device) == 9088);
	static_assert(offsetof(upnp_device, router_address) == 16);
	static_assert(offsetof(upnp_device, internal_address) == 160);
	static_assert(offsetof(upnp_device, socket) == 292);
	static_assert(offsetof(upnp_device, wan_ip_connection) == 296);
	static_assert(offsetof(upnp_device, config) == 300);
	static_assert(offsetof(upnp_device, internal_port) == 472);
	static_assert(offsetof(upnp_device, external_port) == 474);
	static_assert(offsetof(upnp_device, external_address) == 476);
	// A99650 builds the request; A9A2B0 receives the response here.
	static_assert(offsetof(upnp_device, request) == 608);
	static_assert(offsetof(upnp_device, request_size) == 2656);
	static_assert(offsetof(upnp_device, response) == 2660);
	static_assert(offsetof(upnp_device, response_size) == 8804);
	static_assert(offsetof(upnp_device, mapping_attempts) == 9016);

	// A7AE90 constructs this; A7B0C0 stores the WinSock handle as a signed
	// 32-bit value. A7ED20 sign-extends it back to SOCKET when sending.
	struct bd_datagram_socket
	{
		void* vtable;
		std::int32_t handle;
		std::byte pad[4];
	};

	static_assert(sizeof(bd_datagram_socket) == 16);
	static_assert(offsetof(bd_datagram_socket, handle) == 8);

	// A97B70 constructor. Embedded at bdNetImpl+576, next member +20008
	// (A94790); both device offsets and the timer are used by A98380.
	struct alignas(8) upnp_client
	{
		std::uint32_t state;
		std::uint16_t port;
		std::byte pad[2];
		bd_address_array* addresses;
		upnp_config config;
		bd_datagram_socket discovery_socket;
		std::byte discovery[1032];
		upnp_device device;
		upnp_device fallback_device;
		std::uint64_t timer;
		std::uint32_t discovery_attempts;
		bool shutdown_requested;
		std::byte tail[3];
	};

	static_assert(sizeof(upnp_client) == 19432);
	static_assert(offsetof(upnp_client, discovery_socket) == 192);
	static_assert(offsetof(upnp_client, device) == 1240);
	static_assert(offsetof(upnp_client, fallback_device) == 10328);
	static_assert(offsetof(upnp_client, timer) == 19416);
	static_assert(offsetof(upnp_client, discovery_attempts) == 19424);
	static_assert(offsetof(upnp_client, shutdown_requested) == 19428);
}
