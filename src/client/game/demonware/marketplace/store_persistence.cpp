#include <std_include.hpp>

#include "store_persistence.hpp"
#include "game/demonware/achievement/store.hpp"
#include "game/demonware/reward/json.hpp"
#include "game/player_profile.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <string_view>

namespace demonware::marketplace_store::detail
{
	bool is_safe_text(const std::string& value, const std::size_t maximum_length, const bool allow_space)
	{
		if (value.empty() || value.size() > maximum_length)
		{
			return false;
		}

		for (const auto character : value)
		{
			const auto byte = static_cast<unsigned char>(character);
			if (byte > 0x7E || byte < (allow_space ? 0x20 : 0x21))
			{
				return false;
			}
		}

		return true;
	}

	bool is_valid_inventory_record(const inventory_record& record)
	{
		return record.item_id != 0 && record.quantity != 0 &&
			   record.account_type.size() <= max_account_type_length &&
			   (record.account_type.empty() || is_safe_text(record.account_type, max_account_type_length, false)) &&
			   record.item_data.size() <= max_item_data_length &&
			   record.expiry_duration <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
	}

	bool is_valid_response_json(const std::string& response)
	{
		if (response.empty() || response.size() > max_response_json_length)
		{
			return false;
		}

		rapidjson::Document document{};
		document.Parse(response.data(), response.size());
		return !document.HasParseError() && document.IsObject();
	}

	bool is_valid_achievement_state(const std::string& json)
	{
		if (json.empty() || json.size() > max_achievement_state_length)
		{
			return false;
		}
		rapidjson::Document value;
		value.Parse(json.data(), json.size());
		return !value.HasParseError() && value.IsObject() &&
			value.HasMember("achievements") && value["achievements"].IsArray() &&
			value.HasMember("orderActivations") && value["orderActivations"].IsArray();
	}

	namespace persistence
	{
		namespace
		{
			constexpr std::size_t max_store_file_size = 64 * 1024 * 1024;

			char hex_digit(const unsigned int value)
			{
				return static_cast<char>(value < 10 ? '0' + value : 'a' + value - 10);
			}

			std::string hex_encode(const std::string& value)
			{
				std::string result{};
				result.resize(value.size() * 2);
				for (std::size_t i = 0; i < value.size(); ++i)
				{
					const auto byte = static_cast<unsigned char>(value[i]);
					result[i * 2] = hex_digit(byte >> 4);
					result[i * 2 + 1] = hex_digit(byte & 0xF);
				}

				return result;
			}

			std::optional<unsigned int> from_hex_digit(const char value)
			{
				if (value >= '0' && value <= '9')
				{
					return static_cast<unsigned int>(value - '0');
				}

				if (value >= 'a' && value <= 'f')
				{
					return static_cast<unsigned int>(value - 'a' + 10);
				}

				if (value >= 'A' && value <= 'F')
				{
					return static_cast<unsigned int>(value - 'A' + 10);
				}

				return std::nullopt;
			}

			std::optional<std::string> hex_decode(const rapidjson::Value& value)
			{
				if (!value.IsString() || value.GetStringLength() > max_item_data_length * 2 ||
					value.GetStringLength() % 2 != 0)
				{
					return std::nullopt;
				}

				std::string result{};
				result.resize(value.GetStringLength() / 2);
				for (rapidjson::SizeType i = 0; i < value.GetStringLength(); i += 2)
				{
					const auto high = from_hex_digit(value.GetString()[i]);
					const auto low = from_hex_digit(value.GetString()[i + 1]);
					if (!high || !low)
					{
						return std::nullopt;
					}

					result[i / 2] = static_cast<char>((*high << 4) | *low);
				}

				return result;
			}

			bool read_bounded_file(const std::filesystem::path& file, const std::size_t maximum, std::string* data)
			{
				std::ifstream stream{file, std::ios::binary | std::ios::ate};
				if (!stream.is_open())
				{
					return false;
				}

				const auto position = stream.tellg();
				if (position < 0 || static_cast<std::uint64_t>(position) > maximum)
				{
					return false;
				}

				const auto size = static_cast<std::streamsize>(position);
				data->resize(static_cast<std::size_t>(size));
				stream.seekg(0, std::ios::beg);
				if (!data->empty())
				{
					stream.read(data->data(), size);
					return stream.gcount() == size;
				}

				return true;
			}

			bool has_unique_members(const rapidjson::Value& value)
			{
				if (!value.IsObject())
				{
					return false;
				}

				std::set<std::string_view> names{};
				for (auto member = value.MemberBegin(); member != value.MemberEnd(); ++member)
				{
					if (!names.emplace(member->name.GetString(), member->name.GetStringLength()).second)
					{
						return false;
					}
				}

				return true;
			}

			// TODO: Remove this temporary importer after users have had time to upgrade
			// from the standalone achievements.json store shipped on master.
			store_status import_legacy_achievements(state* result)
			{
				const auto legacy_file = player_profile::user_directory() / "achievements.json";
				std::error_code error{};
				const auto exists = std::filesystem::exists(legacy_file, error);
				if (error)
				{
					return store_status::io_error;
				}

				if (!exists)
				{
					*result = {};
					return store_status::ready;
				}

				std::string data{};
				if (!read_bounded_file(legacy_file.c_str(), max_achievement_state_length, &data))
				{
					return store_status::io_error;
				}

				rapidjson::Document legacy{};
				legacy.Parse(data.data(), data.size());
				if (legacy.HasParseError() || !has_unique_members(legacy) || legacy.MemberCount() != 1 ||
					!legacy.HasMember("achievements") || !legacy["achievements"].IsArray())
				{
					return store_status::corrupt;
				}

				rapidjson::Document document{rapidjson::kObjectType};
				auto& allocator = document.GetAllocator();
				rapidjson::Value records{rapidjson::kArrayType};
				std::set<std::string_view> names{};
				for (const auto& value : legacy["achievements"].GetArray())
				{
					if (!has_unique_members(value) || value.MemberCount() != 7 ||
						!value.HasMember("name") || !value["name"].IsString() || !value["name"].GetStringLength() ||
						!value.HasMember("kind") || !value["kind"].IsInt() ||
						!value.HasMember("progress") || !value["progress"].IsUint() || value["progress"].GetUint() > UINT16_MAX ||
						!value.HasMember("progressTarget") || !value["progressTarget"].IsUint() ||
						!value["progressTarget"].GetUint() || value["progressTarget"].GetUint() > UINT16_MAX ||
						!value.HasMember("fulfilledTimes") || !value["fulfilledTimes"].IsInt() ||
						!value.HasMember("completionTimestamp") || !value["completionTimestamp"].IsUint64() ||
						!value.HasMember("status") || !value["status"].IsString())
					{
						return store_status::corrupt;
					}

					const auto status = parse_achievement_status(reward_json::view(value["status"]));
					if (!status || !names.emplace(reward_json::view(value["name"])).second)
					{
						return store_status::corrupt;
					}

					achievement_record record{};
					record.name = reward_json::view(value["name"]);
					record.kind = value["kind"].GetInt();
					record.progress = static_cast<std::uint16_t>(value["progress"].GetUint());
					record.progress_target = value["progressTarget"].GetUint();
					record.fulfilled_times = value["fulfilledTimes"].GetInt();
					record.completion_timestamp = value["completionTimestamp"].GetUint64();
					record.status = *status;
					records.PushBack(serialize_achievement(record, allocator), allocator);
				}

				document.AddMember("achievements", records, allocator);
				document.AddMember("orderActivations", rapidjson::Value{rapidjson::kArrayType}, allocator);
				state imported{};
				imported.achievement_state = reward_json::encode(document);
				if (save(imported) != save_result::saved)
				{
					return store_status::io_error;
				}

				*result = std::move(imported);
				return store_status::ready;
			}

			bool parse_currency(const rapidjson::Value& value, currency_record* record)
			{
				if (!has_unique_members(value) || !value.HasMember("currencyId") || !value["currencyId"].IsUint() ||
					value["currencyId"].GetUint() > 0xFF || !value.HasMember("value") || !value["value"].IsUint())
				{
					return false;
				}

				record->currency_id = static_cast<std::uint8_t>(value["currencyId"].GetUint());
				record->value = value["value"].GetUint();
				return true;
			}

			bool parse_inventory(const rapidjson::Value& value, inventory_record* record)
			{
				if (!has_unique_members(value) || !value.HasMember("playerId") || !value["playerId"].IsUint64() ||
					!value.HasMember("accountType") || !value["accountType"].IsString() ||
					value["accountType"].GetStringLength() > max_account_type_length || !value.HasMember("itemId") ||
					!value["itemId"].IsUint() || !value.HasMember("quantity") || !value["quantity"].IsUint() ||
					!value.HasMember("itemXp") || !value["itemXp"].IsUint() || !value.HasMember("itemDataHex") ||
					!value.HasMember("expireDateTime") || !value["expireDateTime"].IsUint() ||
					!value.HasMember("expiryDuration") || !value["expiryDuration"].IsUint64() ||
					!value.HasMember("collisionField") || !value["collisionField"].IsUint() ||
					value["collisionField"].GetUint() > 0xFFFF || !value.HasMember("modDateTime") ||
					!value["modDateTime"].IsUint())
				{
					return false;
				}

				const auto item_data = hex_decode(value["itemDataHex"]);
				if (!item_data)
				{
					return false;
				}

				record->player_id = value["playerId"].GetUint64();
				record->account_type.assign(value["accountType"].GetString(), value["accountType"].GetStringLength());
				record->item_id = value["itemId"].GetUint();
				record->quantity = value["quantity"].GetUint();
				record->item_xp = value["itemXp"].GetUint();
				record->item_data = *item_data;
				record->expire_date_time = value["expireDateTime"].GetUint();
				record->expiry_duration = value["expiryDuration"].GetUint64();
				record->collision_field = static_cast<std::uint16_t>(value["collisionField"].GetUint());
				record->mod_date_time = value["modDateTime"].GetUint();
				return is_valid_inventory_record(*record);
			}

			bool parse_processed_transaction(const rapidjson::Value& value, committed_economy_transaction* record)
			{
				if (!has_unique_members(value) || !value.HasMember("clientTx") || !value["clientTx"].IsString() ||
					!value.HasMember("requestFingerprint") || !value["requestFingerprint"].IsString() ||
					!value.HasMember("responseJson") || !value["responseJson"].IsString() || !value.HasMember("sequence") ||
					!value["sequence"].IsUint64())
				{
					return false;
				}

				record->client_tx.assign(value["clientTx"].GetString(), value["clientTx"].GetStringLength());
				record->request_fingerprint.assign(value["requestFingerprint"].GetString(),
												   value["requestFingerprint"].GetStringLength());
				record->response_json.assign(value["responseJson"].GetString(), value["responseJson"].GetStringLength());
				record->sequence = value["sequence"].GetUint64();

				return is_safe_text(record->client_tx, max_client_tx_length, false) &&
					   is_safe_text(record->request_fingerprint, max_request_fingerprint_length, true) &&
					   is_valid_response_json(record->response_json) && record->sequence != 0;
			}

			store_status parse_state(const std::string& data, state* result)
			{
				rapidjson::Document document{};
				document.Parse(data.data(), data.size());
				if (document.HasParseError() || !has_unique_members(document) || !document.HasMember("schemaVersion") ||
					!document["schemaVersion"].IsUint())
				{
					return store_status::corrupt;
				}

				const auto version = document["schemaVersion"].GetUint();
				if (version != schema_version)
				{
					return store_status::unsupported_version;
				}

				if (!document.HasMember("currencies") || !document["currencies"].IsArray() ||
					document["currencies"].Size() > 256 || !document.HasMember("inventory") ||
					!document["inventory"].IsArray() || document["inventory"].Size() > max_inventory_records ||
					!document.HasMember("processedTransactions") || !document["processedTransactions"].IsArray() ||
					document["processedTransactions"].Size() > max_processed_transactions ||
					!document.HasMember("nextTransactionSequence") || !document["nextTransactionSequence"].IsUint64() ||
					document["nextTransactionSequence"].GetUint64() == 0)
				{
					return store_status::corrupt;
				}

				state parsed{};
				if (!document.HasMember("achievementState") || !document["achievementState"].IsString())
				{
					return store_status::corrupt;
				}
				parsed.achievement_state.assign(document["achievementState"].GetString(),
					document["achievementState"].GetStringLength());
				if (!is_valid_achievement_state(parsed.achievement_state))
				{
					return store_status::corrupt;
				}

				for (const auto& value : document["currencies"].GetArray())
				{
					currency_record record{};
					if (!parse_currency(value, &record) ||
						!parsed.currencies.emplace(record.currency_id, record.value).second)
					{
						return store_status::corrupt;
					}
				}

				for (const auto& value : document["inventory"].GetArray())
				{
					inventory_record record{};
					if (!parse_inventory(value, &record) ||
						!parsed.inventory.emplace(record.item_id, std::move(record)).second)
					{
						return store_status::corrupt;
					}
				}

				std::set<std::uint64_t> transaction_sequences{};
				std::uint64_t highest_sequence{};
				for (const auto& value : document["processedTransactions"].GetArray())
				{
					committed_economy_transaction record{};
					if (!parse_processed_transaction(value, &record) ||
						!transaction_sequences.emplace(record.sequence).second)
					{
						return store_status::corrupt;
					}

					highest_sequence = std::max(highest_sequence, record.sequence);
					const auto client_tx = record.client_tx;
					if (!parsed.processed_transactions.emplace(client_tx, std::move(record)).second)
					{
						return store_status::corrupt;
					}
				}

				parsed.next_transaction_sequence = document["nextTransactionSequence"].GetUint64();
				if (parsed.next_transaction_sequence <= highest_sequence)
				{
					return store_status::corrupt;
				}

				*result = std::move(parsed);
				return store_status::ready;
			}

			void add_string(rapidjson::Value* object, const char* name, const std::string& value,
							rapidjson::Document::AllocatorType& allocator)
			{
				object->AddMember(rapidjson::Value{name, allocator},
								  rapidjson::Value{value.data(), static_cast<rapidjson::SizeType>(value.size()), allocator},
								  allocator);
			}

			std::optional<std::string> serialize_state(const state& state)
			{
				rapidjson::Document document{};
				document.SetObject();
				auto& allocator = document.GetAllocator();
				document.AddMember("schemaVersion", schema_version, allocator);
				if (!is_valid_achievement_state(state.achievement_state))
				{
					return std::nullopt;
				}
				add_string(&document, "achievementState", state.achievement_state, allocator);

				rapidjson::Value currencies{rapidjson::kArrayType};
				for (const auto& [currency_id, value] : state.currencies)
				{
					rapidjson::Value currency{rapidjson::kObjectType};
					currency.AddMember("currencyId", currency_id, allocator);
					currency.AddMember("value", value, allocator);
					currencies.PushBack(currency, allocator);
				}
				document.AddMember("currencies", currencies, allocator);

				rapidjson::Value inventory{rapidjson::kArrayType};
				for (const auto& [item_id, record] : state.inventory)
				{
					rapidjson::Value item{rapidjson::kObjectType};
					item.AddMember("playerId", record.player_id, allocator);
					add_string(&item, "accountType", record.account_type, allocator);
					item.AddMember("itemId", item_id, allocator);
					item.AddMember("quantity", record.quantity, allocator);
					item.AddMember("itemXp", record.item_xp, allocator);
					add_string(&item, "itemDataHex", hex_encode(record.item_data), allocator);
					item.AddMember("expireDateTime", record.expire_date_time, allocator);
					item.AddMember("expiryDuration", record.expiry_duration, allocator);
					item.AddMember("collisionField", record.collision_field, allocator);
					item.AddMember("modDateTime", record.mod_date_time, allocator);
					inventory.PushBack(item, allocator);
				}
				document.AddMember("inventory", inventory, allocator);

				std::vector<const committed_economy_transaction*> transactions{};
				transactions.reserve(state.processed_transactions.size());
				for (const auto& entry : state.processed_transactions)
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

				rapidjson::Value processed_transactions{rapidjson::kArrayType};
				for (const auto* record : transactions)
				{
					rapidjson::Value transaction{rapidjson::kObjectType};
					add_string(&transaction, "clientTx", record->client_tx, allocator);
					add_string(&transaction, "requestFingerprint", record->request_fingerprint, allocator);
					add_string(&transaction, "responseJson", record->response_json, allocator);
					transaction.AddMember("sequence", record->sequence, allocator);
					processed_transactions.PushBack(transaction, allocator);
				}
				document.AddMember("processedTransactions", processed_transactions, allocator);
				document.AddMember("nextTransactionSequence", state.next_transaction_sequence, allocator);

				rapidjson::StringBuffer buffer{};
				rapidjson::Writer<rapidjson::StringBuffer> writer{buffer};
				document.Accept(writer);
				if (buffer.GetSize() > max_store_file_size)
				{
					return std::nullopt;
				}

				return std::string{buffer.GetString(), buffer.GetSize()};
			}

			bool write_all(const HANDLE file, const std::string& data)
			{
				std::size_t offset{};
				while (offset < data.size())
				{
					const auto remaining = data.size() - offset;
					const auto block_size =
						static_cast<DWORD>(std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max()));
					DWORD written{};
					if (WriteFile(file, data.data() + offset, block_size, &written, nullptr) == FALSE ||
						written != block_size)
					{
						return false;
					}

					offset += written;
				}

				return true;
			}

		}

		save_result save(const state& state)
		{
			const auto marketplace_file = player_profile::user_directory() / "marketplace.json";
			const auto marketplace_temporary_file = player_profile::user_directory() / "marketplace.json.tmp";
			std::optional<std::string> serialized{};
			try
			{
				serialized = serialize_state(state);
			}
			catch (const std::exception&)
			{
				return save_result::io_error;
			}

			if (!serialized)
			{
				return save_result::too_large;
			}

			std::error_code error{};
			std::filesystem::create_directories(std::filesystem::path{marketplace_file}.parent_path(), error);
			if (error)
			{
				return save_result::io_error;
			}

			const auto temporary_file = CreateFileW(marketplace_temporary_file.c_str(), GENERIC_WRITE, 0, nullptr,
													CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (temporary_file == INVALID_HANDLE_VALUE)
			{
				return save_result::io_error;
			}

			const auto wrote_file = write_all(temporary_file, *serialized) && FlushFileBuffers(temporary_file) != FALSE;
			const auto closed_file = CloseHandle(temporary_file) != FALSE;
			if (!wrote_file || !closed_file)
			{
				DeleteFileW(marketplace_temporary_file.c_str());
				return save_result::io_error;
			}

			if (MoveFileExW(marketplace_temporary_file.c_str(), marketplace_file.c_str(),
							MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
			{
				DeleteFileW(marketplace_temporary_file.c_str());
				return save_result::io_error;
			}

			return save_result::saved;
		}

		store_status load(state* result)
		{
			const auto marketplace_file = player_profile::user_directory() / "marketplace.json";
			std::error_code error{};
			const auto exists = std::filesystem::exists(marketplace_file, error);
			if (error)
			{
				return store_status::io_error;
			}

			if (!exists)
			{
				return import_legacy_achievements(result);
			}

			if (!std::filesystem::is_regular_file(marketplace_file, error) || error)
			{
				return error ? store_status::io_error : store_status::corrupt;
			}

			const auto size = std::filesystem::file_size(marketplace_file, error);
			if (error)
			{
				return store_status::io_error;
			}

			if (size == 0 || size > max_store_file_size)
			{
				return store_status::corrupt;
			}

			std::string data{};
			if (!read_bounded_file(marketplace_file.c_str(), max_store_file_size, &data))
			{
				return store_status::io_error;
			}

			state parsed{};
			store_status status{};
			try
			{
				status = parse_state(data, &parsed);
			}
			catch (const std::exception&)
			{
				return store_status::corrupt;
			}

			if (status != store_status::ready)
			{
				return status;
			}

			*result = std::move(parsed);
			return store_status::ready;
		}
	}
}
