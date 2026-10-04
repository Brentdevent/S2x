#pragma once

#include "marketplace_store.hpp"

namespace demonware
{
	namespace achievement_kind
	{
		constexpr bool special(const int kind)
		{
			return kind == 3;
		}

		constexpr bool order(const int kind)
		{
			return kind == 1 || kind == 2 || special(kind) || kind == 8 || kind == 9;
		}

		constexpr bool contract(const int kind)
		{
			return kind == 4 || kind == 11;
		}

		constexpr bool periodic(const int kind)
		{
			return order(kind) || contract(kind);
		}

		constexpr bool zombies(const int kind)
		{
			return kind == 8 || kind == 9 || kind == 11;
		}

		constexpr bool in_mode(const int kind, const bool zm)
		{
			return periodic(kind) && zombies(kind) == zm;
		}
	}

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
		std::uint32_t progress_target{1};
		int fulfilled_times{1};
		std::uint64_t completion_timestamp{};
		achievement_status status{achievement_status::finished};
		bool requires_claim{};
		std::optional<std::uint64_t> activation_timestamp{};
		std::optional<std::uint64_t> expiration_timestamp{};
		std::optional<std::int32_t> usage_time_target{};
		std::optional<std::int32_t> usage_time_remaining{};
		std::optional<std::uint32_t> global_progress_target{};
		std::optional<std::uint32_t> global_counter_id{};
		std::string success_rewards{"[]"};
	};

	const char* get_achievement_status_name(achievement_status status);
	std::optional<achievement_status> parse_achievement_status(std::string_view name);
	rapidjson::Value serialize_achievement(const achievement_record& record,
		rapidjson::Document::AllocatorType& allocator);

	namespace achievement_store
	{
		enum class mutation_result
		{
			unchanged,
			updated,
			save_failed,
		};

		struct order_offer
		{
			achievement_record achievement;
			std::uint32_t activation_limit{};
			std::uint64_t next_period_start{};
			std::uint32_t cost_item_id{};
			std::uint64_t period_start{};
			bool repeatable{};
		};

		enum class activation_result
		{
			success,
			not_scheduled,
			already_completed,
			limit_reached,
			transaction_conflict,
			invalid_state,
			missing_token,
			save_failed,
		};

		// grant is false when a previously finished Order is claimed with a new ClientTx
		marketplace_store::transaction_result claim_order(const std::string& name,
			std::uint64_t user_id, const std::string& transaction, std::uint64_t timestamp,
			const std::function<bool(marketplace_store::transaction&, const achievement_record&,
				bool grant, const achievement_record* bonus, std::string& response)>& reward);

		activation_result activate_order(const std::string& name, int kind,
			std::uint64_t user_id, const std::string& transaction,
			const std::optional<order_offer>& offer, std::uint64_t timestamp,
			std::uint32_t* cost_item_id = nullptr);

		marketplace_store::transaction_result deactivate_order(const std::string& name,
			std::uint64_t user_id, const std::string& transaction);

		bool complete_hq_reward(achievement_record record, std::uint64_t timestamp,
			std::uint64_t event_timestamp, bool payroll,
			const std::function<bool(marketplace_store::transaction&, const achievement_record&, bool)>& reward);

		std::vector<achievement_record> get_all();

		mutation_result merge_completion_bits(const std::vector<achievement_record>& records,
			const std::function<bool(marketplace_store::transaction&)>& reward = {});

		mutation_result mutate(const std::string& name,
			const std::function<bool(achievement_record&)>& mutator,
			const std::function<bool(marketplace_store::transaction&)>& reward = {});

		mutation_result mutate_all(const std::function<bool(achievement_record&)>& mutator);
	}
}
