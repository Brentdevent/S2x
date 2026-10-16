#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace demonware::marketplace_store
{
	inline constexpr std::uint32_t schema_version = 1;
	inline constexpr std::size_t max_achievement_state_length = 8 * 1024 * 1024;
	inline constexpr std::size_t max_account_type_length = 10;
	inline constexpr std::size_t max_item_data_length = 64;
	inline constexpr std::size_t max_client_tx_length = 64;
	inline constexpr std::size_t max_request_fingerprint_length = 256;
	// Stock response buffers hold 6144 bytes, including the NUL.
	inline constexpr std::size_t max_response_json_length = 6144 - 1;
	inline constexpr std::size_t max_inventory_records = 65536;
	// Retain receipts to prevent duplicate grants or consumption on retries.
	inline constexpr std::size_t max_processed_transactions = 65536;

	struct currency_record
	{
		std::uint8_t currency_id{};
		std::uint32_t value{};

		bool operator==(const currency_record&) const = default;
	};

	struct inventory_record
	{
		std::uint64_t player_id{};
		std::string account_type{};
		std::uint32_t item_id{};
		std::uint32_t quantity{};
		std::uint32_t item_xp{};
		std::string item_data{};
		std::uint32_t expire_date_time{};
		std::uint64_t expiry_duration{};
		std::uint16_t collision_field{};
		std::uint32_t mod_date_time{};

		bool operator==(const inventory_record&) const = default;
	};

	inline bool is_permanent(const inventory_record& item)
	{
		return (!item.expire_date_time && !item.expiry_duration) ||
			(item.expire_date_time == UINT32_MAX && item.expiry_duration == INT64_MAX);
	}

	// Local save ownership only; incoming requests must still validate the current user.
	bool is_saved_profile_user(std::uint64_t saved_user, std::uint64_t current_user);
	bool owns_inventory(const inventory_record& item, std::uint64_t current_user);

	// Permanent receipts for economy mutations, not progress-only events.
	struct committed_economy_transaction
	{
		std::string client_tx{};
		std::string request_fingerprint{};
		std::string response_json{};
		std::uint64_t sequence{};

		bool operator==(const committed_economy_transaction&) const = default;
	};

	enum class store_status
	{
		ready,
		corrupt,
		unsupported_version,
		io_error,
	};

	struct snapshot
	{
		store_status status{store_status::ready};
		std::vector<currency_record> currencies{};
		std::vector<inventory_record> inventory{};
	};

	enum class edit_result
	{
		unchanged,
		updated,
		invalid_argument,
		insufficient_quantity,
		overflow,
		capacity_exceeded,
	};

	struct achievement_snapshot
	{
		store_status status{store_status::ready};
		std::string json;
	};

	class transaction;
	struct transaction_result;
	using transaction_callback = std::function<bool(transaction&, std::string& response_json)>;

	class transaction
	{
	public:
		transaction(const transaction&) = delete;
		transaction& operator=(const transaction&) = delete;

		const std::string& get_achievement_state() const;
		bool set_achievement_state(const std::string& json);

		std::uint32_t get_currency(std::uint8_t currency_id) const;
		std::optional<inventory_record> get_inventory(std::uint32_t item_id) const;
		std::vector<inventory_record> get_inventory() const;

		edit_result add_currency(std::uint8_t currency_id, std::uint32_t amount);
		edit_result consume_currency(std::uint8_t currency_id, std::uint32_t amount);

		edit_result consume_inventory(std::uint32_t item_id, std::uint32_t quantity);
		edit_result set_inventory(const inventory_record& record);

	private:
		struct state_view;
		explicit transaction(state_view* state);

		state_view* state_{};

		friend bool save_achievement_state(const std::string&, const std::function<bool(transaction&)>&);
		friend transaction_result transact(const std::string&, const std::string&, const transaction_callback&);
	};

	enum class transaction_status
	{
		committed,
		replayed,
		client_tx_conflict,
		rejected,
		invalid_argument,
		store_unavailable,
		capacity_exceeded,
		save_failed,
	};

	struct transaction_result
	{
		transaction_status status{transaction_status::invalid_argument};
		std::string response_json{};
	};

	// Progress and inventory are saved atomically in the same file.
	achievement_snapshot get_achievement_state();
	bool save_achievement_state(const std::string& json,
		const std::function<bool(transaction&)>& reward = {});
	snapshot get_snapshot();
	std::optional<committed_economy_transaction> find_transaction(const std::string& client_tx);

	// Runs under the store lock; do not call other marketplace_store functions.
	// Return false to abort, or true with a valid JSON response to commit.
	transaction_result transact(const std::string& client_tx, const std::string& request_fingerprint,
								const transaction_callback& callback);

	// Matches client receipts across GUID changes without rewriting them.
	// Other operation arguments must match; dedicated servers remain user-scoped.
	transaction_result transact_profile(const std::string& client_tx, std::uint64_t user,
		const std::string& prefix, const std::string& suffix, const transaction_callback& callback);

	// hash_request(owner) returns the legacy SHA-256 hex digest.
	// Clients hash with owner zero; legacy matches require a saved owner ID.
	transaction_result transact_profile_hashed(const std::string& client_tx, std::uint64_t user,
		const std::string& prefix, const std::function<std::string(std::uint64_t)>& hash_request,
		const transaction_callback& callback);
}
