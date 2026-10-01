#include <std_include.hpp>
#include "reward_json.hpp"

#include <unordered_set>

namespace demonware::reward_json
{
	bool bounded_ascii(const std::string_view value, const std::size_t maximum,
		const bool allow_space)
	{
		return !value.empty() && value.size() <= maximum &&
			std::ranges::all_of(value, [allow_space](const char c)
			{
				const auto byte = static_cast<unsigned char>(c);
				return byte >= (allow_space ? 0x20 : 0x21) && byte <= 0x7E;
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
		std::unordered_set<std::string_view> names;
		for (auto member = object.MemberBegin(); member != object.MemberEnd(); ++member)
		{
			if (!names.emplace(member->name.GetString(), member->name.GetStringLength()).second)
			{
				return false;
			}
		}
		return true;
	}

	bool common_fields(const rapidjson::Value& object, const std::string_view action,
		std::string& transaction, const bool exact_transaction_length)
	{
		return unique_members(object) && object.HasMember("Version") &&
			object["Version"].IsInt() && object["Version"].GetInt() == 0 &&
			object.HasMember("Action") && object["Action"].IsString() &&
			std::string_view{object["Action"].GetString(), object["Action"].GetStringLength()} == action &&
			object.HasMember("ClientTx") &&
			client_tx(object["ClientTx"], transaction, exact_transaction_length);
	}

	void add_string(rapidjson::Value& object, const char* name,
		const std::string_view value, rapidjson::Document::AllocatorType& allocator)
	{
		object.AddMember(rapidjson::Value{name, allocator},
			rapidjson::Value{value.data(), static_cast<rapidjson::SizeType>(value.size()), allocator},
			allocator);
	}

	void add_detailed_inventory(rapidjson::Value& array,
		const marketplace_store::inventory_record& record,
		rapidjson::Document::AllocatorType& allocator)
	{
		rapidjson::Value item{rapidjson::kObjectType};
		item.AddMember("item_id", record.item_id, allocator);
		item.AddMember("collision_field", record.collision_field, allocator);
		if (record.expiry_duration == 0)
		{
			item.AddMember("expiry_duration", rapidjson::Value{rapidjson::kNullType}, allocator);
		}
		else
		{
			item.AddMember("expiry_duration", record.expiry_duration, allocator);
		}

		item.AddMember("item_quantity", record.quantity, allocator);
		item.AddMember("mod_date_time", record.mod_date_time, allocator);
		array.PushBack(item, allocator);
	}

	std::uint32_t modification_time()
	{
		const auto current = time(nullptr);
		if (current <= 0)
		{
			return 0;
		}

		return static_cast<std::uint32_t>(std::min<std::uint64_t>(
			static_cast<std::uint64_t>(current), std::numeric_limits<std::uint32_t>::max()));
	}

}
