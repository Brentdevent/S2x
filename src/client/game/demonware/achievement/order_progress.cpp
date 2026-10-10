#include <std_include.hpp>
#include "order_progress.hpp"

#include <charconv>
#include <limits>

namespace demonware::order_progress
{
	bool timed(const achievement_record& record)
	{
		return achievement_kind::contract(record.kind) && record.usage_time_target.value_or(0) > 0 &&
			record.usage_time_remaining.value_or(0) > 0 &&
			*record.usage_time_remaining <= *record.usage_time_target;
	}

	bool eligible(const achievement_record& record)
	{
		return ((achievement_kind::order(record.kind) &&
			!record.usage_time_target && !record.usage_time_remaining) || timed(record)) &&
			record.status == achievement_status::in_progress && record.requires_claim &&
			record.fulfilled_times == 0 && record.activation_timestamp.value_or(0) &&
			!record.expiration_timestamp &&
			!record.global_counter_id && !record.global_progress_target &&
			record.progress_target <= std::numeric_limits<std::uint16_t>::max() &&
			record.progress < record.progress_target;
	}

	bool supported(std::string_view definition, const predicate& native)
	{
		if (definition.empty())
		{
			return native.count == 0 && native.operation == 0;
		}

		if (definition.size() > 31 || !native.count || native.count > 3 ||
			(native.operation != 0 && native.operation != 1))
		{
			return false;
		}

		for (unsigned i = 0; i < native.count; ++i)
		{
			if (!definition.starts_with('('))
			{
				return false;
			}

			definition.remove_prefix(1);
			unsigned selector{};
			auto parsed = std::from_chars(definition.data(), definition.data() + definition.size(), selector);
			if (parsed.ec != std::errc{} || parsed.ptr == definition.data() + definition.size() ||
				*parsed.ptr != ':' || selector != native.selectors[i])
			{
				return false;
			}

			definition.remove_prefix(parsed.ptr - definition.data() + 1);
			std::int32_t value{};
			parsed = std::from_chars(definition.data(), definition.data() + definition.size(), value);
			if (parsed.ec != std::errc{} || parsed.ptr == definition.data() + definition.size() ||
				*parsed.ptr != ')' || static_cast<std::uint32_t>(value) != native.values[i])
			{
				return false;
			}

			definition.remove_prefix(parsed.ptr - definition.data() + 1);
			if (i + 1 == native.count)
			{
				return definition.empty();
			}

			if (!definition.starts_with(native.operation ? "||" : "&&"))
			{
				return false;
			}

			definition.remove_prefix(2);
		}

		return false;
	}

	std::optional<server_predicate> server_rule(const std::string_view name, const int kind, const int event_id,
		const std::string_view unit)
	{
		if (achievement_kind::special(kind) && event_id == 5 && unit == "AEC_UNIT_MATCHES_COMPLETED")
		{
			// Script 1128 emits end_game 1:1 for players present at the full match end.
			// _gamelogic returns before this call at round/War halftime transitions.
			return server_predicate{{0, 1, {1}, {1}}};
		}

		// These restored offers have no predicate in dwGameChallenges.csv: class 0
		// is evaluated by the retail backend, not the client's prediction cache.
		// Local policy bindings use the stock script 1128 producers: sniper
		// multi-kill 1:7, multi-kill 2:1; domCap 3:1; end_game mode/result/placement;
		// theater vendor 1:1. This is not a recovered backend rule configuration.
		// Keep the missing conditions as data and use the same predicate evaluator.
		struct rule { std::string_view name; int kind, event; server_predicate condition; };
		static constexpr rule rules[] = {
			{"daily_ch_karloot2_1", 1, 2, {{0, 2, {1, 2}, {7, 1}}}},
			{"contract_3_multikills", 4, 2, {{0, 1, {2}, {1}}}},
			{"contract_ch_dom_caps", 4, 3, {{0, 1, {3}, {1}}}},
			{"weekly_ch_ab_warwins", 2, 5, {{0, 2, {2, 3}, {5, 1}}}},
			{"daily_ch_tdm_first", 1, 5, {{0, 2, {2, 6}, {1, 1}}}},
			{"daily_ch_theater_watch", 1, 11, {{0, 1, {1}, {1}}}},
			// _achievement_engine_z_utils: purchase event 36 uses selector 1 for
			// trap/weapon/door; special event 38 uses it for revive/stun/all doors.
			{"daily_zm_ch_traps", 8, 36, {{0, 1, {1}, {1}}}},
			{"daily_zm_ch_purchase_weapons_1", 8, 36, {{0, 1, {1}, {2}}}},
			{"weekly_zm_ch_purchase_doors_1", 9, 36, {{0, 1, {1}, {4}}}},
			{"daily_zm_ch_revives_1", 8, 38, {{0, 1, {1}, {2}}}},
			{"contract_zm_shellshock_1", 11, 38, {{0, 1, {1}, {16}}}},
			{"contract_zm_open_all_doors_1", 11, 38, {{0, 1, {1}, {22}}}},
			// _events_z reports native streaks only at round completion. The script
			// owns resets for damage, weapon use and leaving an area. These local
			// bindings use stock card requirements (5/5/10/20 waves); do not add the
			// cumulative counter on each report or count survival_wave_single (2:8).
			{"daily_zm_ch_wave_challenge_dmg_1", 8, 37, {{0, 1, {2}, {3}}, 1, 5}},
			{"daily_zm_ch_wave_challenge_shotgun_1", 8, 37, {{0, 1, {2}, {5}}, 1, 5}},
			{"contract_zm_wave_location_2", 11, 37, {{0, 3, {2, 3, 5}, {4, 9, 2}}, 1, 10}},
			{"contract_zm_wave_time_trial_1", 11, 37, {{0, 1, {2}, {1}}, 1, 20}},
			// Final Reich's Panzermorder emits 9 (casual) or 13 (hardcore), not
			// any_objective_completed (20), which belongs to stage objectives.
			{"weekly_zm_ch_objective_2", 9, 38, {{1, 2, {1, 1}, {9, 13}}}},
		};

		for (const auto& rule : rules)
		{
			if (rule.name == name && rule.kind == kind && rule.event == event_id)
			{
				return rule.condition;
			}
		}

		return {};
	}

	bool matches(const predicate& rule, const event& occurrence)
	{
		if (rule.count > 3 || occurrence.count > 10 ||
			(rule.operation != 0 && rule.operation != 1))
		{
			return false;
		}

		if (!rule.count)
		{
			return true;
		}

		bool any{};
		for (unsigned i = 0; i < rule.count; ++i)
		{
			bool matched{};
			for (unsigned j = 0; j < occurrence.count; ++j)
			{
				if (rule.selectors[i] != occurrence.selectors[j])
				{
					continue;
				}

				matched |= rule.selectors[i] < 128 ? rule.values[i] == occurrence.values[j] : (rule.values[i] & occurrence.values[j]) != 0;
			}

			if (!matched && !rule.operation)
			{
				return false;
			}

			any |= matched;
		}

		return any;
	}

	bool matches(const server_predicate& rule, const event& occurrence)
	{
		if (!matches(rule.condition, occurrence))
		{
			return false;
		}

		if (!rule.counter_minimum)
		{
			return true;
		}

		for (unsigned i = 0; i < occurrence.count; ++i)
		{
			if (occurrence.selectors[i] == rule.counter_selector)
			{
				return occurrence.values[i] >= rule.counter_minimum &&
					occurrence.values[i] <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
			}
		}

		return false;
	}

	achievement_store::mutation_result settle(const std::vector<target>& targets,
		const std::uint64_t completion_time, const std::vector<usage>& elapsed)
	{
		if (targets.empty() && elapsed.empty())
		{
			return achievement_store::mutation_result::unchanged;
		}

		return achievement_store::mutate_all([&](achievement_record& record)
		{
			if (!eligible(record))
			{
				return false;
			}

			const auto same_activation = [&](const target& expected)
			{
				return record.name == expected.name && record.kind == expected.kind &&
					record.activation_timestamp == expected.activation && record.progress_target == expected.progress_target;
			};

			bool changed{};
			if (timed(record))
			{
				const auto usage = std::ranges::find_if(elapsed, [&](const auto& value)
				{
					return same_activation(value.achievement) && value.remaining >= 0;
				});
				// Contract events require an observation of their native match clock.
				if (usage == elapsed.end())
				{
					return false;
				}

				const auto remaining = std::min(*record.usage_time_remaining, usage->remaining);
				changed = remaining != *record.usage_time_remaining;
				record.usage_time_remaining = remaining;

				// Local boundary policy: native AE expiry tests elapsed >= remaining.
				// Settle that deadline before an occurrence at the same whole second.
				// Earlier claimable completions are frozen by eligible(), including retries.
				if (!remaining)
				{
					// Inactive + zero usage is our durable expired tombstone; the native
					// listener presents it through reason=expired, not a fifth status enum.
					record.status = achievement_status::inactive;
					record.expiration_timestamp = completion_time;
					return true;
				}
			}

			if (std::ranges::find_if(targets, same_activation) == targets.end())
			{
				return changed;
			}

			++record.progress;
			if (record.progress == record.progress_target)
			{
				record.status = achievement_status::claimable;
				record.completion_timestamp = completion_time;
				record.fulfilled_times = 1;
			}

			return true;
		});
	}
}
