#include <std_include.hpp>

#include "store.hpp"
#include "store_persistence.hpp"
#include "game/game.hpp"

#include <algorithm>
#include <charconv>
#include <set>

namespace demonware::marketplace_store
{
	namespace
	{
		using store_state = detail::state;
		using detail::is_safe_text;
		using detail::is_valid_inventory_record;
		using detail::is_valid_response_json;
		using detail::save_result;

		std::mutex store_mutex{};
		store_state current_state{};
		store_status current_status{store_status::ready};
		bool store_loaded{};

		void load_store()
		{
			if (store_loaded)
			{
				return;
			}

			store_state loaded{};
			current_status = detail::persistence::load(&loaded);
			store_loaded = current_status != store_status::io_error;
			if (current_status == store_status::ready)
			{
				current_state = std::move(loaded);
			}
		}

		std::optional<std::uint64_t> parse_saved_user(const std::string_view text)
		{
			std::uint64_t user{};
			const auto parsed = std::from_chars(text.data(), text.data() + text.size(), user);
			if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && user &&
				std::to_string(user) == text)
			{
				return user;
			}

			return {};
		}

		std::set<std::uint64_t> saved_profile_users()
		{
			std::lock_guard lock{store_mutex};
			load_store();

			std::set<std::uint64_t> users;
			if (current_status != store_status::ready)
			{
				return users;
			}

			for (const auto& [id, item] : current_state.inventory)
			{
				if (item.player_id)
				{
					users.insert(item.player_id);
				}
			}

			// These legacy receipt formats explicitly stored the owner. They can
			// still identify it after every original inventory row has been used.
			constexpr std::array prefixes
			{
				"purchase:"sv,
				"collection:"sv,
				"pawn:"sv,
				"activate_contract:"sv,
				"abandon_order:"sv,
				"claim_order:"sv,
				"giveItem:"sv,
				"giveCurrency:"sv
			};

			for (const auto& [key, receipt] : current_state.processed_transactions)
			{
				auto owner = std::string_view{receipt.request_fingerprint};
				if (owner.starts_with("open_supply_drop|"))
				{
					owner.remove_prefix(owner.rfind('|') + 1);
				}
				else if (!key.starts_with("promotions:"))
				{
					const auto prefix = std::ranges::find_if(prefixes, [&](const auto value)
					{
						return owner.starts_with(value);
					});

					if (prefix == prefixes.end())
					{
						continue;
					}

					owner.remove_prefix(prefix->size());
					owner = owner.substr(0, owner.find(':'));
				}

				if (const auto user = parse_saved_user(owner))
				{
					users.insert(*user);
				}
			}

			rapidjson::Document achievements;
			achievements.Parse(current_state.achievement_state.data(), current_state.achievement_state.size());
			if (!achievements.HasParseError() && achievements.IsObject() &&
				achievements.HasMember("orderActivations") && achievements["orderActivations"].IsArray())
			{
				for (const auto& receipt : achievements["orderActivations"].GetArray())
				{
					if (receipt.IsObject() && receipt.HasMember("userID") &&
						receipt["userID"].IsUint64() && receipt["userID"].GetUint64())
					{
						users.insert(receipt["userID"].GetUint64());
					}
				}
			}

			return users;
		}

		void compact_transaction_sequences(store_state* state)
		{
			std::vector<committed_economy_transaction*> transactions{};
			transactions.reserve(state->processed_transactions.size());
			for (auto& entry : state->processed_transactions)
			{
				transactions.push_back(&entry.second);
			}

			std::sort(transactions.begin(), transactions.end(),
					  [](const committed_economy_transaction* left, const committed_economy_transaction* right) {
						  if (left->sequence != right->sequence)
						  {
							  return left->sequence < right->sequence;
						  }

						  return left->client_tx < right->client_tx;
					  });

			std::uint64_t sequence{1};
			for (auto* transaction : transactions)
			{
				transaction->sequence = sequence++;
			}

			state->next_transaction_sequence = sequence;
		}

	} // namespace

	struct transaction::state_view
	{
		store_state* state{};
	};

	bool is_saved_profile_user(const std::uint64_t saved_user, const std::uint64_t current_user)
	{
		return current_user && (!game::environment::is_dedicated() || saved_user == current_user);
	}

	bool owns_inventory(const inventory_record& item, const std::uint64_t current_user)
	{
		return current_user && (!item.player_id || is_saved_profile_user(item.player_id, current_user)) &&
			(item.account_type.empty() || item.account_type == "steam");
	}

	transaction::transaction(state_view* state) : state_(state)
	{
	}

	const std::string& transaction::get_achievement_state() const
	{
		return state_->state->achievement_state;
	}

	bool transaction::set_achievement_state(const std::string& json)
	{
		if (!detail::is_valid_achievement_state(json))
		{
			return false;
		}

		state_->state->achievement_state = json;
		return true;
	}

	achievement_snapshot get_achievement_state()
	{
		std::lock_guard lock{store_mutex};
		load_store();
		return {current_status, current_state.achievement_state};
	}

	bool save_achievement_state(const std::string& json,
		const std::function<bool(transaction&)>& reward)
	{
		if (!detail::is_valid_achievement_state(json))
		{
			return false;
		}

		std::lock_guard lock{store_mutex};
		load_store();
		if (current_status != store_status::ready)
		{
			return false;
		}

		auto staged = current_state;
		staged.achievement_state = json;
		transaction::state_view view{&staged};
		transaction economy{&view};
		if (reward && !reward(economy))
		{
			return false;
		}

		if (detail::persistence::save(staged) != save_result::saved)
		{
			return false;
		}

		current_state = std::move(staged);
		return true;
	}

	std::uint32_t transaction::get_currency(const std::uint8_t currency_id) const
	{
		const auto entry = this->state_->state->currencies.find(currency_id);
		return entry == this->state_->state->currencies.end() ? 0 : entry->second;
	}

	std::optional<inventory_record> transaction::get_inventory(const std::uint32_t item_id) const
	{
		const auto entry = this->state_->state->inventory.find(item_id);
		if (entry == this->state_->state->inventory.end())
		{
			return std::nullopt;
		}

		return entry->second;
	}

	std::vector<inventory_record> transaction::get_inventory() const
	{
		std::vector<inventory_record> result{};
		result.reserve(this->state_->state->inventory.size());
		for (const auto& entry : this->state_->state->inventory)
		{
			result.push_back(entry.second);
		}

		return result;
	}

	edit_result transaction::add_currency(const std::uint8_t currency_id, const std::uint32_t amount)
	{
		if (amount == 0)
		{
			return edit_result::unchanged;
		}

		const auto current = this->get_currency(currency_id);
		if (amount > std::numeric_limits<std::uint32_t>::max() - current)
		{
			return edit_result::overflow;
		}

		this->state_->state->currencies[currency_id] = current + amount;
		return edit_result::updated;
	}

	edit_result transaction::consume_currency(const std::uint8_t currency_id, const std::uint32_t amount)
	{
		if (amount == 0)
		{
			return edit_result::invalid_argument;
		}

		const auto current = this->get_currency(currency_id);
		if (current < amount)
		{
			return edit_result::insufficient_quantity;
		}

		this->state_->state->currencies[currency_id] = current - amount;
		return edit_result::updated;
	}

	edit_result transaction::consume_inventory(const std::uint32_t item_id, const std::uint32_t quantity)
	{
		if (item_id == 0 || quantity == 0)
		{
			return edit_result::invalid_argument;
		}

		auto& inventory = this->state_->state->inventory;
		const auto entry = inventory.find(item_id);
		if (entry == inventory.end() || entry->second.quantity < quantity)
		{
			return edit_result::insufficient_quantity;
		}

		if (entry->second.quantity == quantity)
		{
			inventory.erase(entry);
		}
		else
		{
			entry->second.quantity -= quantity;
		}

		return edit_result::updated;
	}

	edit_result transaction::set_inventory(const inventory_record& record)
	{
		if (record.item_id == 0)
		{
			return edit_result::invalid_argument;
		}

		auto& inventory = this->state_->state->inventory;
		const auto entry = inventory.find(record.item_id);
		if (record.quantity == 0)
		{
			if (entry == inventory.end())
			{
				return edit_result::unchanged;
			}

			inventory.erase(entry);
			return edit_result::updated;
		}

		if (!is_valid_inventory_record(record))
		{
			return edit_result::invalid_argument;
		}

		if (entry == inventory.end())
		{
			if (inventory.size() >= max_inventory_records)
			{
				return edit_result::capacity_exceeded;
			}

			inventory.emplace(record.item_id, record);
			return edit_result::updated;
		}

		if (entry->second == record)
		{
			return edit_result::unchanged;
		}

		entry->second = record;
		return edit_result::updated;
	}

	snapshot get_snapshot()
	{
		std::lock_guard lock{store_mutex};
		load_store();

		snapshot result{};
		result.status = current_status;
		if (current_status != store_status::ready)
		{
			return result;
		}

		result.currencies.reserve(current_state.currencies.size());
		for (const auto& [currency_id, value] : current_state.currencies)
		{
			result.currencies.push_back({currency_id, value});
		}

		result.inventory.reserve(current_state.inventory.size());
		for (const auto& entry : current_state.inventory)
		{
			result.inventory.push_back(entry.second);
		}

		return result;
	}

	std::optional<committed_economy_transaction> find_transaction(const std::string& client_tx)
	{
		std::lock_guard lock{store_mutex};
		load_store();
		if (current_status != store_status::ready)
		{
			return std::nullopt;
		}

		const auto found = current_state.processed_transactions.find(client_tx);
		return found == current_state.processed_transactions.end() ? std::nullopt :
			std::optional{found->second};
	}

	transaction_result transact_profile(const std::string& client_tx, const std::uint64_t user,
		const std::string& prefix, const std::string& suffix, const transaction_callback& callback)
	{
		if (!user)
		{
			return {};
		}

		if (game::environment::is_dedicated())
		{
			return transact(client_tx, prefix + std::to_string(user) + suffix, callback);
		}

		auto fingerprint = prefix + "profile" + suffix;
		if (const auto previous = find_transaction(client_tx))
		{
			const auto& saved = previous->request_fingerprint;
			if (saved.size() > prefix.size() + suffix.size() &&
				saved.starts_with(prefix) && saved.ends_with(suffix))
			{
				const auto owner = std::string_view{saved}.substr(prefix.size(),
					saved.size() - prefix.size() - suffix.size());
				if (parse_saved_user(owner))
				{
					fingerprint = saved;
				}
			}
		}

		// Accepted receipts are immutable and never evicted. transact still checks
		// under its lock, so concurrent first requests cannot grant twice.
		return transact(client_tx, fingerprint, callback);
	}

	transaction_result transact_profile_hashed(const std::string& client_tx, const std::uint64_t user,
		const std::string& prefix, const std::function<std::string(std::uint64_t)>& hash_request,
		const transaction_callback& callback)
	{
		if (!user || !hash_request)
		{
			return {};
		}

		if (game::environment::is_dedicated())
		{
			return transact(client_tx, prefix + hash_request(user), callback);
		}

		// Zero is only a canonical hash input, never an accepted request owner.
		auto fingerprint = prefix + "profile:" + hash_request(0);
		if (const auto previous = find_transaction(client_tx))
		{
			const auto& saved = previous->request_fingerprint;
			if (saved.starts_with(prefix) && saved.size() == prefix.size() + 64)
			{
				auto users = saved_profile_users();
				users.insert(user);

				for (const auto old_user : users)
				{
					if (saved == prefix + hash_request(old_user))
					{
						fingerprint = saved;
						break;
					}
				}
			}
		}

		// Never accept an opaque legacy hash without matching the full request.
		// Missing historical identity evidence remains a conflict, not a regrant.
		return transact(client_tx, fingerprint, callback);
	}

	transaction_result transact(const std::string& client_tx, const std::string& request_fingerprint,
								const transaction_callback& callback)
	{
		transaction_result result{};
		if (!callback || !is_safe_text(client_tx, max_client_tx_length, false) ||
			!is_safe_text(request_fingerprint, max_request_fingerprint_length, true))
		{
			return result;
		}

		std::lock_guard lock{store_mutex};
		load_store();
		if (current_status != store_status::ready)
		{
			result.status = transaction_status::store_unavailable;
			return result;
		}

		const auto existing = current_state.processed_transactions.find(client_tx);
		if (existing != current_state.processed_transactions.end())
		{
			if (existing->second.request_fingerprint != request_fingerprint)
			{
				result.status = transaction_status::client_tx_conflict;
				return result;
			}

			result.status = transaction_status::replayed;
			result.response_json = existing->second.response_json;
			return result;
		}

		// No S2 transaction-expiry contract was verified. Evicting an accepted ID
		// would eventually let the same request grant again, so preserve all known
		// keys and refuse new mutations when the bounded ledger is full.
		if (current_state.processed_transactions.size() >= max_processed_transactions)
		{
			result.status = transaction_status::capacity_exceeded;
			return result;
		}

		auto staged = current_state;
		transaction::state_view view{&staged};
		transaction edit{&view};
		std::string response_json{};
		try
		{
			if (!callback(edit, response_json))
			{
				result.status = transaction_status::rejected;
				return result;
			}
		}
		catch (const std::exception&)
		{
			result.status = transaction_status::rejected;
			return result;
		}
		catch (...)
		{
			result.status = transaction_status::rejected;
			return result;
		}

		if (!is_valid_response_json(response_json))
		{
			result.status = transaction_status::invalid_argument;
			return result;
		}

		if (staged.next_transaction_sequence == std::numeric_limits<std::uint64_t>::max())
		{
			compact_transaction_sequences(&staged);
		}

		committed_economy_transaction record{};
		record.client_tx = client_tx;
		record.request_fingerprint = request_fingerprint;
		record.response_json = response_json;
		record.sequence = staged.next_transaction_sequence++;
		staged.processed_transactions.emplace(client_tx, std::move(record));

		const auto saved = detail::persistence::save(staged);
		if (saved == save_result::too_large)
		{
			result.status = transaction_status::capacity_exceeded;
			return result;
		}

		if (saved != save_result::saved)
		{
			result.status = transaction_status::save_failed;
			return result;
		}

		current_state = std::move(staged);
		result.status = transaction_status::committed;
		result.response_json = std::move(response_json);
		return result;
	}
}
