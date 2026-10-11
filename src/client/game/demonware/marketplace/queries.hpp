#pragma once

#include "game/demonware/byte_buffer.hpp"
#include "game/demonware/data_types.hpp"
#include <memory>
#include <vector>

namespace demonware::marketplace_queries
{
	struct result
	{
		std::uint32_t error{};
		std::vector<std::unique_ptr<bdTaskResult>> records;
	};
	result balance(byte_buffer* buffer);
	result inventory(byte_buffer* buffer, std::uint64_t fallback_user_id, bool dedicated);
}
