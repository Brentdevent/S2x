#pragma once

#include "game/demonware/loot/catalog.hpp"
#include "action.hpp"
#include "game/demonware/servers/service_server.hpp"

namespace demonware::reward_task4
{
	struct execution_context
	{
		std::uint64_t user_id{};
		bool dedicated{};
		std::shared_ptr<const loot_catalog::catalog> catalog{};
		std::uint32_t modification_time{};
		float loot_rarity_scale{1.0f};
	};

	std::uint32_t handle(service_server* server, byte_buffer* buffer, const execution_context& context);
}
