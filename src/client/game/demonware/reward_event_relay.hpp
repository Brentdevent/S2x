#pragma once

#include "order_progress.hpp"

#include <span>

namespace demonware::reward_event_relay
{
	inline constexpr auto command = "$s2x_rg1";
	inline constexpr std::size_t batch_limit = 4;
	inline constexpr std::size_t pending_limit = 120;
	inline constexpr std::size_t payload_limit = 800;

	enum class operation : std::uint8_t
	{
		event,
		start,
		time,
		stop,
	};

	struct record
	{
		std::uint64_t sequence{};
		operation type{};
		std::uint32_t seconds{};
		std::uint8_t event_class{};
		order_progress::event event{};
	};

	struct batch
	{
		std::uint64_t user{};
		std::uint64_t stream{};
		bool zombies{};
		std::vector<record> records{};
	};

	std::string encode(std::uint64_t user, std::uint64_t stream, bool zombies, std::span<const record> records);
	std::optional<batch> decode(std::string_view payload);
}
