#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace demonware::marketplace_store
{
	// One current S2x economy format; earlier development schemas are unsupported.
	inline constexpr std::uint32_t schema_version = 4;
	inline constexpr std::size_t max_achievement_state_length = 8 * 1024 * 1024;
	inline constexpr std::size_t max_account_type_length = 10;
	inline constexpr std::size_t max_item_data_length = 64;
	inline constexpr std::size_t max_client_tx_length = 64;
	inline constexpr std::size_t max_request_fingerprint_length = 256;
	// Stock reward action response buffers are 6144 bytes including the NUL.
	inline constexpr std::size_t max_response_json_length = 6144 - 1;
	inline constexpr std::size_t max_inventory_records = 65536;
	// Keep accepted IDs: replenished stock makes old consume/open/purchase retries
	// unsafe to forget. Allow years of ordinary play without an eviction policy.
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

	// Accepted atomic economy mutation and the exact response returned on replay.
	// Progress-only events must not enter this permanent, non-evicting ledger.
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
		std::vector<committed_economy_transaction> processed_transactions{};
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
	enum class mutation_result;
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
		std::vector<currency_record> get_currencies() const;
		std::vector<inventory_record> get_inventory() const;

		edit_result set_currency(std::uint8_t currency_id, std::uint32_t value);
		edit_result add_currency(std::uint8_t currency_id, std::uint32_t amount);
		edit_result consume_currency(std::uint8_t currency_id, std::uint32_t amount);

		// The supplied quantity is added to the current quantity. Its remaining
		// metadata replaces the stored metadata for the item.
		edit_result grant_inventory(const inventory_record& grant);
		edit_result consume_inventory(std::uint32_t item_id, std::uint32_t quantity);
		edit_result set_inventory(const inventory_record& record);

	private:
		struct state_view;
		explicit transaction(state_view* state);

		state_view* state_{};

		friend bool save_achievement_state(const std::string&, const std::function<bool(transaction&)>&);
		friend mutation_result set_currency(std::uint8_t, std::uint32_t);
		friend mutation_result grant_inventory(const inventory_record&);
		friend mutation_result consume_inventory(std::uint32_t, std::uint32_t);
		friend transaction_result transact(const std::string&, const std::string&, const transaction_callback&);
	};

	enum class mutation_result
	{
		unchanged,
		updated,
		invalid_argument,
		insufficient_quantity,
		overflow,
		capacity_exceeded,
		store_unavailable,
		save_failed,
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

	// Achievement state shares the authoritative economy file and atomic save.
	achievement_snapshot get_achievement_state();
	bool save_achievement_state(const std::string& json,
		const std::function<bool(transaction&)>& reward = {});
	snapshot get_snapshot();
	std::optional<committed_economy_transaction> find_transaction(const std::string& client_tx);
	store_status get_status();

	mutation_result set_currency(std::uint8_t currency_id, std::uint32_t value);
	mutation_result grant_inventory(const inventory_record& grant);
	mutation_result consume_inventory(std::uint32_t item_id, std::uint32_t quantity);

	// The callback runs synchronously while the store is locked and must not
	// call another marketplace_store function. Returning false aborts without
	// changing memory or disk. A successful callback must provide valid JSON.
	transaction_result transact(const std::string& client_tx, const std::string& request_fingerprint,
								const transaction_callback& callback);
}
