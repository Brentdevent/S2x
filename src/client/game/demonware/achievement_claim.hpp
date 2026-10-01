#pragma once

#include <cstdint>
#include <string>
#include "achievement_store.hpp"

namespace demonware
{
	class byte_buffer;
	class service_server;

	namespace achievement_claim
	{
		inline constexpr auto action = "claim_achievement_reward";
		struct result
		{
			std::uint32_t error{};
			std::string acknowledgement;
			std::string achievement_push;
		};

		bool settle_reward(marketplace_store::transaction& economy, const achievement_record& record,
			bool grant, std::uint64_t user, std::uint32_t timestamp, std::string& response);

		result process(const std::string& json, std::uint64_t user_id, std::uint32_t timestamp);
		// Handles claims and terminal notifications after native achievement refreshes.
		// Leaves unrelated Task 4 actions and their input buffers untouched.
		bool try_handle(service_server* server, byte_buffer* buffer);
	}
}
