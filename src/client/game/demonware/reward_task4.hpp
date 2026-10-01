#pragma once

#include "loot_catalog.hpp"
#include "reward_action.hpp"
#include "servers/service_server.hpp"

namespace demonware::reward_task4
{
	struct execution_context
	{
		std::uint64_t user_id{};
		bool dedicated{};
		std::shared_ptr<const loot_catalog::catalog> catalog;
		std::uint32_t modification_time{};
		float loot_rarity_scale{1.0f};
	};
	struct result
	{
		reward::action_result action;
		std::string context;
		std::uint64_t user_id{};
	};

	// Owns envelope validation and dispatch, not frontend access or loot policy.
	result process(byte_buffer* buffer, const execution_context& context);
	void deliver(service_server* server, const result& response);
	result handle(service_server* server, byte_buffer* buffer, const execution_context& context);
}
