#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace network::routing
{
	// The engine's deferred packet queue accepts at most 1262 payload bytes.
	inline constexpr std::size_t max_payload_size = 1262;
	// Guest packets: OOB marker + "s2x_route " + source/target nibbles + native payload.
	// Both peers must support this envelope; primary traffic remains compatible with older S2x.
	inline constexpr std::string_view routing_header = "\xFF\xFF\xFF\xFFs2x_route ";
	inline constexpr std::size_t routing_overhead = routing_header.size() + 1;
	inline constexpr std::size_t max_wire_size = max_payload_size + routing_overhead;

	inline bool needs_routing(const int source, const int target)
	{
		return source == 1 || target == 1;
	}

	inline bool valid_net_id(const int id)
	{
		// Two local clients (0, 1) and the server/broadcast destination (2).
		return id >= 0 && id <= 2;
	}

	inline std::size_t encode(const std::span<char> output, const std::string_view payload,
		const int source, const int target)
	{
		if (payload.empty() || payload.size() > max_payload_size
			|| !valid_net_id(source) || !valid_net_id(target))
		{
			return 0;
		}

		const auto overhead = needs_routing(source, target) ? routing_overhead : 0;
		if (output.size() < payload.size() + overhead)
		{
			return 0;
		}

		if (overhead)
		{
			std::memcpy(output.data(), routing_header.data(), routing_header.size());
			output[routing_header.size()] = static_cast<char>(source | (target << 4));
		}

		std::memcpy(output.data() + overhead, payload.data(), payload.size());
		return payload.size() + overhead;
	}

	inline bool decode(std::string_view& payload, int& source, int& target)
	{
		source = 0;
		target = 0;
		if (!payload.starts_with(routing_header))
		{
			// Keep the existing wire format for primary clients, RCON and discovery.
			return true;
		}

		if (payload.size() <= routing_overhead || payload.size() > max_wire_size)
		{
			return false;
		}

		const auto route = static_cast<std::uint8_t>(payload[routing_header.size()]);
		const auto packet_source = route & 0xF;
		const auto packet_target = route >> 4;
		if (!valid_net_id(packet_source) || !valid_net_id(packet_target)
			|| !needs_routing(packet_source, packet_target))
		{
			return false;
		}

		source = packet_source;
		target = packet_target;
		payload.remove_prefix(routing_overhead);
		return true;
	}
}
