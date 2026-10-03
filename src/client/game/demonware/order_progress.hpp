#pragma once

#include "achievement_store.hpp"
#include <array>
#include <cstddef>
#include <string_view>

namespace demonware::order_progress
{
	// S2 MP reward-event payload and AE_ParseChallengeDependencies output.
	struct event
	{
		std::int32_t id{};
		std::uint8_t count{};
		std::array<std::uint8_t, 10> selectors{};
		std::uint8_t padding{};
		std::array<std::uint32_t, 10> values{};
		std::uint64_t timestamp{};
		std::uint64_t user_id{};
	};
	static_assert(sizeof(event) == 72);
	static_assert(offsetof(event, values) == 16);
	static_assert(offsetof(event, timestamp) == 56);

	struct predicate
	{
		std::int32_t operation{};
		std::uint8_t count{};
		std::array<std::uint8_t, 3> selectors{};
		std::array<std::uint32_t, 3> values{};
	};
	static_assert(sizeof(predicate) == 20);
	static_assert(offsetof(predicate, values) == 8);

	// Backend-only conditions expressed against native script event fields.
	// A counter threshold tests an absolute observation; it is never an increment.
	struct server_predicate
	{
		predicate condition;
		std::uint8_t counter_selector{};
		std::uint32_t counter_minimum{};
	};

	struct target
	{
		std::string name;
		int kind{};
		std::uint64_t activation{};
		std::uint32_t progress_target{};
	};

	struct usage
	{
		target achievement;
		std::int32_t remaining{};
	};

	bool timed(const achievement_record& record);
	bool eligible(const achievement_record& record);
	// Reject definitions the stock fixed-size parser would truncate or ignore.
	bool supported(std::string_view definition, const predicate& native);
	std::optional<server_predicate> server_rule(std::string_view name, int kind, int event_id);
	bool matches(const predicate& rule, const event& occurrence);
	bool matches(const server_predicate& rule, const event& occurrence);
	achievement_store::mutation_result settle(const std::vector<target>& targets,
		std::uint64_t completion_time, const std::vector<usage>& usage = {});
}
