#include <std_include.hpp>
#include "player_vote.hpp"
#include "struct_buffer_reader.hpp"

#include <utils/string.hpp>

namespace demonware::player_vote
{
	namespace
	{
		std::string receipt(const bool received, const std::uint64_t giver,
			const std::uint64_t recipient, const std::uint32_t day)
		{
			return utils::string::va("commend:%c:%llx:%llx:%u", received ? 'r' : 'g', giver, recipient, day);
		}

		void integer(std::string& bytes, std::uint64_t value)
		{
			do
			{
				bytes += static_cast<char>((value & 127) | (value > 127 ? 128 : 0));
				value >>= 7;
			} while (value);
		}

		void object(std::string& bytes, const unsigned tag, const std::string& value)
		{
			integer(bytes, (tag << 3) | 2);
			integer(bytes, value.size());
			bytes += value;
		}

		bool account(const std::string_view bytes, std::uint64_t& user)
		{
			struct_buffer_reader reader{bytes};
			bool has_user{}, has_type{};
			while (!reader.empty())
			{
				std::uint32_t tag{};
				std::uint8_t wire{};
				std::string_view type;
				if (!reader.read_tag(tag, wire))
				{
					return false;
				}

				if (tag == 1 && wire == 0 && !has_user)
				{
					has_user = reader.read_varint(user) && user;
					if (!has_user)
					{
						return false;
					}
				}
				else if (tag == 2 && wire == 2 && !has_type)
				{
					has_type = reader.read_length_delimited(type) && type == "steam";
					if (!has_type)
					{
						return false;
					}
				}
				else
				{
					return false;
				}
			}

			return has_user && has_type;
		}
	}

	std::uint32_t period()
	{
		return static_cast<std::uint32_t>(time(nullptr) / 86400);
	}

	bool parse_request(byte_buffer* buffer, std::vector<std::uint64_t>& users)
	{
		users.clear();
		std::string bytes;
		if (!buffer || !buffer->read_struct(&bytes, 4096) || !buffer->has_only_zero_padding())
		{
			return false;
		}

		// SDK 0xA40110/0xA3FF50: context (1), one or more account objects (2).
		struct_buffer_reader reader{bytes};
		bool context{};
		while (!reader.empty())
		{
			std::uint32_t tag{};
			std::uint8_t wire{};
			std::string_view value;
			if (!reader.read_tag(tag, wire) || wire != 2 || !reader.read_length_delimited(value))
			{
				return false;
			}

			if (tag == 1 && !context && value == "s2")
			{
				context = true;
			}
			else if (tag == 2 && users.size() < 66)
			{
				std::uint64_t user{};
				if (!account(value, user))
				{
					return false;
				}
				users.push_back(user);
			}
			else
			{
				return false;
			}
		}

		return context && !users.empty();
	}

	bool was_given(const std::uint64_t giver, const std::uint64_t recipient, const std::uint32_t day)
	{
		return marketplace_store::find_transaction(receipt(false, giver, recipient, day)).has_value();
	}

	std::string vote_status(const std::uint64_t giver, const std::vector<std::uint64_t>& users, const std::uint32_t day)
	{
		std::string bytes;
		for (const auto user : users)
		{
			std::string identity, row;
			integer(identity, 8);
			integer(identity, user);
			object(identity, 2, "steam");
			object(row, 1, identity);
			integer(row, 16);
			// 0xA44D90 -> 0x1E36A0: true means CanVote, not AlreadyVoted.
			integer(row, giver && user != giver && !was_given(giver, user, day));
			object(bytes, 1, row);
		}
		return bytes;
	}

	marketplace_store::transaction_result receive(const std::uint64_t giver,
		const std::uint64_t recipient, const std::uint32_t day)
	{
		if (!giver || !recipient || giver == recipient || !day)
		{
			return {};
		}

		const auto key = receipt(true, giver, recipient, day);
		return marketplace_store::transact(key, key, [](auto& transaction, auto& response)
		{
			response = "{}";
			return transaction.get_currency(social_currency) <= INT32_MAX - social_score &&
				transaction.add_currency(social_currency, social_score) == marketplace_store::edit_result::updated;
		});
	}

	marketplace_store::transaction_result record_given(const std::uint64_t giver,
		const std::uint64_t recipient, const std::uint32_t day)
	{
		if (!giver || !recipient || giver == recipient || !day)
		{
			return {};
		}

		const auto key = receipt(false, giver, recipient, day);
		return marketplace_store::transact(key, key, [](auto&, auto& response)
		{
			response = "{}";
			return true;
		});
	}
}
