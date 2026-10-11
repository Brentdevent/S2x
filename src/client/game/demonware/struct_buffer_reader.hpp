#pragma once

#include <cstdint>
#include <limits>
#include <string_view>

namespace demonware
{
	class struct_buffer_reader
	{
	public:
		explicit struct_buffer_reader(const std::string_view data)
			: data_(data)
		{
		}

		bool empty() const
		{
			return data_.empty();
		}

		bool read_tag(std::uint32_t& field, std::uint8_t& wire_type)
		{
			std::uint64_t tag{};
			if (!read_varint(tag) || tag > std::numeric_limits<std::uint32_t>::max())
			{
				return false;
			}

			field = static_cast<std::uint32_t>(tag >> 3);
			wire_type = static_cast<std::uint8_t>(tag & 7);
			return field != 0;
		}

		bool read_varint(std::uint64_t& value)
		{
			value = 0;
			for (auto index = 0u; index < 10; ++index)
			{
				std::uint8_t byte{};
				if (!read_byte(byte) || (index == 9 && (byte & 0xFE) != 0))
				{
					return false;
				}

				value |= static_cast<std::uint64_t>(byte & 0x7F) << (index * 7);
				if ((byte & 0x80) == 0)
				{
					return true;
				}
			}

			return false;
		}

		bool read_length_delimited(std::string_view& value)
		{
			std::uint64_t length{};
			if (!read_varint(length) || length > data_.size())
			{
				return false;
			}

			value = data_.substr(0, static_cast<std::size_t>(length));
			data_.remove_prefix(static_cast<std::size_t>(length));
			return true;
		}

		bool skip_field(const std::uint8_t wire_type)
		{
			switch (wire_type)
			{
			case 0:
			{
				std::uint64_t value{};
				return read_varint(value);
			}
			case 1:
				return skip_bytes(8);
			case 2:
			{
				std::string_view value{};
				return read_length_delimited(value);
			}
			case 5:
				return skip_bytes(4);
			default:
				return false;
			}
		}

	private:
		bool read_byte(std::uint8_t& value)
		{
			if (data_.empty())
			{
				return false;
			}

			value = static_cast<std::uint8_t>(data_.front());
			data_.remove_prefix(1);
			return true;
		}

		bool skip_bytes(const std::size_t count)
		{
			if (count > data_.size())
			{
				return false;
			}

			data_.remove_prefix(count);
			return true;
		}

		std::string_view data_{};
	};
}
