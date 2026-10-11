#pragma once

#include "byte_buffer.hpp"
#include "game/demonware/marketplace/store.hpp"

namespace demonware::player_vote
{
	inline constexpr std::uint8_t social_currency = 7;
	inline constexpr std::uint32_t social_score = 25;

	// Local policy: one commendation per giver/recipient pair per UTC day.
	// The retail receive_commendation reward is 25 Social Score; its cooldown
	// policy was backend-owned and is not present in the captured descriptor.
	std::uint32_t period();
	bool parse_request(byte_buffer* buffer, std::vector<std::uint64_t>& users);
	std::string vote_status(std::uint64_t giver, const std::vector<std::uint64_t>& users, std::uint32_t day);
	bool was_given(std::uint64_t giver, std::uint64_t recipient, std::uint32_t day);
	marketplace_store::transaction_result receive(std::uint64_t giver, std::uint64_t recipient, std::uint32_t day);
	marketplace_store::transaction_result record_given(std::uint64_t giver, std::uint64_t recipient, std::uint32_t day);
}
