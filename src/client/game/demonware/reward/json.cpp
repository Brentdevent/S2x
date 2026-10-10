#include <std_include.hpp>
#include "json.hpp"

namespace demonware::reward_json
{
	bool bounded_ascii(const std::string_view value, const std::size_t maximum, const bool allow_space)
	{
		if (value.empty() || value.size() > maximum)
		{
			return false;
		}

		const auto minimum = static_cast<unsigned char>(allow_space ? 0x20 : 0x21);
		return std::ranges::all_of(value, [minimum](const char c)
		{
			const auto byte = static_cast<unsigned char>(c);
			return byte >= minimum && byte <= 0x7E;
		});
	}

	bool client_tx(const rapidjson::Value& value, std::string& result, const bool exact_length)
	{
		if (!value.IsString())
		{
			return false;
		}

		const std::string_view text{value.GetString(), value.GetStringLength()};
		if (!bounded_ascii(text, 24) || (exact_length && text.size() != 24))
		{
			return false;
		}

		result.assign(text);
		return true;
	}

	bool unique_members(const rapidjson::Value& object)
	{
		if (!object.IsObject())
		{
			return false;
		}

		std::unordered_set<std::string_view> names{};
		for (auto member = object.MemberBegin(); member != object.MemberEnd(); ++member)
		{
			if (!names.emplace(member->name.GetString(), member->name.GetStringLength()).second)
			{
				return false;
			}
		}

		return true;
	}

	const rapidjson::Value* find_member(const rapidjson::Value& object, const char* name)
	{
		if (!object.IsObject())
		{
			return nullptr;
		}

		const auto member = object.FindMember(name);
		return member != object.MemberEnd() ? &member->value : nullptr;
	}

	std::string_view view(const rapidjson::Value& value)
	{
		return {value.GetString(), value.GetStringLength()};
	}

	bool equals(const rapidjson::Value* value, const std::string_view text)
	{
		return value && value->IsString() && view(*value) == text;
	}

	bool ascii_string(const rapidjson::Value* value, const std::size_t maximum)
	{
		return value && value->IsString() && bounded_ascii(view(*value), maximum);
	}

	bool common_fields(const rapidjson::Value& object, const std::string_view action, std::string& transaction,
		const bool exact_transaction_length)
	{
		if (!unique_members(object))
		{
			return false;
		}

		const auto version = object.FindMember("Version");
		const auto name = object.FindMember("Action");
		const auto client_transaction = object.FindMember("ClientTx");
		if (version == object.MemberEnd() || !version->value.IsInt() || version->value.GetInt() != 0 ||
			name == object.MemberEnd() || !name->value.IsString() ||
			client_transaction == object.MemberEnd())
		{
			return false;
		}

		return std::string_view{name->value.GetString(), name->value.GetStringLength()} == action &&
			client_tx(client_transaction->value, transaction, exact_transaction_length);
	}

	void add_string(rapidjson::Value& object, const char* name, const std::string_view value,
		rapidjson::Document::AllocatorType& allocator)
	{
		object.AddMember(rapidjson::Value{name, allocator},
			rapidjson::Value{value.data(), static_cast<rapidjson::SizeType>(value.size()), allocator}, allocator);
	}

	rapidjson::Value inventory_row(const marketplace_store::inventory_record& record,
		rapidjson::Document::AllocatorType& allocator)
	{
		rapidjson::Value expiry{rapidjson::kNullType};
		if (record.expiry_duration)
		{
			expiry.SetUint64(record.expiry_duration);
		}

		rapidjson::Value row{rapidjson::kObjectType};
		row.AddMember("item_id", record.item_id, allocator);
		row.AddMember("collision_field", record.collision_field, allocator);
		row.AddMember("expiry_duration", expiry, allocator);
		row.AddMember("item_quantity", record.quantity, allocator);
		row.AddMember("mod_date_time", record.mod_date_time, allocator);
		return row;
	}

	void add_detailed_inventory(rapidjson::Value& array, const marketplace_store::inventory_record& record,
		rapidjson::Document::AllocatorType& allocator)
	{
		array.PushBack(inventory_row(record, allocator), allocator);
	}

	std::string encode(const rapidjson::Value& value)
	{
		rapidjson::StringBuffer buffer{};
		rapidjson::Writer<rapidjson::StringBuffer> writer{buffer};
		value.Accept(writer);

		return {buffer.GetString(), buffer.GetSize()};
	}

	std::uint32_t modification_time()
	{
		const auto now = time(nullptr);
		if (now <= 0)
		{
			return 0;
		}

		return static_cast<std::uint32_t>(std::min<std::uint64_t>(now, std::numeric_limits<std::uint32_t>::max()));
	}
}
