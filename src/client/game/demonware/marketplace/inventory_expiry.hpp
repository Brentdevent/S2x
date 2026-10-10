#pragma once

#include <cstdint>
#include <limits>

namespace demonware::inventory_expiry
{
	struct wire_value
	{
		std::uint32_t date;
		std::int64_t duration;
	};

	inline wire_value to_wire(const std::uint32_t date, const std::uint64_t duration)
	{
		// Local legacy grants use 0/0 for permanent inventory. S2 0x279650
		// instead treats either zero as expired. Match the native record defaults
		// (0x20D200 / SDK 0xA49540), also preserved by task-4 JSON null duration.
		// Do not rewrite any explicitly supplied finite expiry metadata or storage.
		if (!date && !duration)
		{
			return {std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::int64_t>::max()};
		}

		return {date, static_cast<std::int64_t>(duration)};
	}
}
