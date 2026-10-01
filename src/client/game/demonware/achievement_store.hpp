#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace demonware
{
	enum class achievement_status
	{
		inactive = 1,
		in_progress = 2,
		claimable = 3,
		finished = 4,
	};

	struct achievement_record
	{
		std::string name{};
		int kind{1};
		std::uint16_t progress{};
		std::uint16_t progress_target{1};
		int fulfilled_times{1};
		std::uint64_t completion_timestamp{};
		achievement_status status{achievement_status::finished};
		bool requires_claim{};
		std::uint64_t activation_timestamp{};
		std::uint64_t expiration_timestamp{};
		std::uint32_t usage_time_target{};
		std::uint32_t reward_item{};
		std::uint32_t reward_currency{};
		std::uint32_t reward_amount{};
	};

	const char* get_achievement_status_name(achievement_status status);

	namespace achievement_store
	{
		enum class mutation_result
		{
			unchanged,
			updated,
			save_failed,
		};

		std::vector<achievement_record> get_all();
		bool merge(const std::vector<achievement_record>& records);
		mutation_result mutate(const std::string& name,
			const std::function<bool(achievement_record&)>& mutator);
		bool erase(const std::string& name);
	}
}
