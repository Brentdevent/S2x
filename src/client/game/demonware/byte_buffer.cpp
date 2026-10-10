#include <std_include.hpp>
#include "dw_include.hpp"

namespace demonware
{
	bool byte_buffer::read_bool(bool* output)
	{
		if (!this->read_data_type(BD_BB_BOOL_TYPE)) return false;
		return this->read(1, output);
	}

	bool byte_buffer::read_byte(char* output)
	{
		if (!this->read_data_type(BD_BB_SIGNED_CHAR8_TYPE)) return false;
		return this->read(sizeof(*output), output);
	}

	bool byte_buffer::read_ubyte(unsigned char* output)
	{
		if (!this->read_data_type(BD_BB_UNSIGNED_CHAR8_TYPE)) return false;
		return this->read(sizeof(*output), output);
	}

	bool byte_buffer::read_int16(short* output)
	{
		if (!this->read_data_type(BD_BB_SIGNED_INTEGER16_TYPE)) return false;
		return this->read(sizeof(*output), output);
	}

	bool byte_buffer::read_uint16(unsigned short* output)
	{
		if (!this->read_data_type(BD_BB_UNSIGNED_INTEGER16_TYPE)) return false;
		return this->read(sizeof(*output), output);
	}

	bool byte_buffer::read_int32(int* output)
	{
		if (!this->read_data_type(BD_BB_SIGNED_INTEGER32_TYPE)) return false;
		return this->read(sizeof(*output), output);
	}

	bool byte_buffer::read_uint32(unsigned int* output)
	{
		if (!this->read_data_type(BD_BB_UNSIGNED_INTEGER32_TYPE)) return false;
		return this->read(sizeof(*output), output);
	}

	bool byte_buffer::read_int64(__int64* output)
	{
		if (!this->read_data_type(BD_BB_SIGNED_INTEGER64_TYPE)) return false;
		return this->read(sizeof(*output), output);
	}

	bool byte_buffer::read_uint64(unsigned __int64* output)
	{
		if (!this->read_data_type(BD_BB_UNSIGNED_INTEGER64_TYPE)) return false;
		return this->read(sizeof(*output), output);
	}

	bool byte_buffer::read_float(float* output)
	{
		if (!this->read_data_type(BD_BB_FLOAT32_TYPE)) return false;
		return this->read(sizeof(*output), output);
	}

	bool byte_buffer::read_string(std::string* output)
	{
		return this->read_string(output, this->remaining_size());
	}

	bool byte_buffer::read_string(std::string* output, const size_t maximum_size)
	{
		if (!output || !this->read_data_type(BD_BB_SIGNED_CHAR8_STRING_TYPE) ||
			this->current_byte_ > this->buffer_.size())
		{
			return false;
		}

		const auto remaining = this->buffer_.size() - this->current_byte_;
		const auto* start = this->buffer_.data() + this->current_byte_;
		const auto* terminator = static_cast<const char*>(std::memchr(start, '\0', remaining));
		if (!terminator)
		{
			return false;
		}

		const auto length = static_cast<size_t>(terminator - start);
		if (length > maximum_size)
		{
			return false;
		}

		output->assign(start, length);
		this->current_byte_ += length + 1;
		return true;
	}

	bool byte_buffer::read_string(char** output)
	{
		if (!output || !this->read_data_type(BD_BB_SIGNED_CHAR8_STRING_TYPE) ||
			this->current_byte_ > this->buffer_.size())
		{
			return false;
		}

		const auto remaining = this->buffer_.size() - this->current_byte_;
		auto* start = const_cast<char*>(this->buffer_.data()) + this->current_byte_;
		const auto* terminator = static_cast<const char*>(std::memchr(start, '\0', remaining));
		if (!terminator)
		{
			return false;
		}

		*output = start;
		this->current_byte_ += static_cast<size_t>(terminator - start) + 1;
		return true;
	}

	bool byte_buffer::read_string(char* output, const int length)
	{
		if (!output || length <= 0 || !this->read_data_type(BD_BB_SIGNED_CHAR8_STRING_TYPE) ||
			this->current_byte_ > this->buffer_.size())
		{
			return false;
		}

		const auto remaining = this->buffer_.size() - this->current_byte_;
		const auto* start = this->buffer_.data() + this->current_byte_;
		const auto* terminator = static_cast<const char*>(std::memchr(start, '\0', remaining));
		if (!terminator)
		{
			return false;
		}

		const auto string_size = static_cast<size_t>(terminator - start);
		if (string_size >= static_cast<size_t>(length))
		{
			return false;
		}

		std::memcpy(output, start, string_size + 1);
		this->current_byte_ += string_size + 1;
		return true;
	}

	bool byte_buffer::read_blob(std::string* output)
	{
		return this->read_blob(output, this->remaining_size());
	}

	bool byte_buffer::read_blob(std::string* output, const size_t maximum_size)
	{
		if (!output || !this->read_data_type(BD_BB_BLOB_TYPE))
		{
			return false;
		}

		unsigned int size{};
		if (!this->read_uint32(&size) || size > maximum_size ||
			this->current_byte_ > this->buffer_.size() ||
			size > this->buffer_.size() - this->current_byte_)
		{
			return false;
		}

		output->assign(this->buffer_.data() + this->current_byte_, size);
		this->current_byte_ += size;
		return true;
	}

	bool byte_buffer::read_blob(char** output, int* length)
	{
		if (!output || !length || !this->read_data_type(BD_BB_BLOB_TYPE))
		{
			return false;
		}

		unsigned int size{};
		if (!this->read_uint32(&size) || size > static_cast<unsigned int>(INT_MAX) ||
			this->current_byte_ > this->buffer_.size() ||
			size > this->buffer_.size() - this->current_byte_)
		{
			return false;
		}

		*output = const_cast<char*>(this->buffer_.data()) + this->current_byte_;
		*length = static_cast<int>(size);

		this->current_byte_ += size;

		return true;
	}

	bool byte_buffer::read_struct(void* output)
	{
		if (!output || !this->read_data_type(BD_BB_STRUCTURED_DATA_TYPE))
		{
			return false;
		}

		unsigned int size{};
		if (!this->read_uint32(&size) || this->current_byte_ > this->buffer_.size() ||
			size > this->buffer_.size() - this->current_byte_)
		{
			return false;
		}

		auto data = const_cast<char*>(this->buffer_.data()) + this->current_byte_;
		memcpy(output, data, size);

		this->current_byte_ += size;

		return true;
	}

	bool byte_buffer::read_struct(std::string* output, const size_t maximum_size)
	{
		if (!output || !this->read_data_type(BD_BB_STRUCTURED_DATA_TYPE))
		{
			return false;
		}

		unsigned int size{};
		if (!this->read_uint32(&size) || size > maximum_size ||
			this->current_byte_ > this->buffer_.size() ||
			size > this->buffer_.size() - this->current_byte_)
		{
			return false;
		}

		output->assign(this->buffer_.data() + this->current_byte_, size);
		this->current_byte_ += size;
		return true;
	}

	bool byte_buffer::read_data_type(const unsigned char expected)
	{
		if (!this->use_data_types_) return true;

		unsigned char type{};
		return this->read(sizeof(type), &type) && type == expected;
	}

	bool byte_buffer::read_array_header(const unsigned char expected, unsigned int* element_count,
	                                    unsigned int* element_size)
	{
		if (element_count)
		{
			*element_count = 0;
		}
		if (element_size)
		{
			*element_size = 0;
		}

		if (!this->read_data_type(expected + 100))
		{
			return false;
		}

		uint32_t array_size{}, el_count{};
		if (!this->read_uint32(&array_size))
		{
			return false;
		}

		const auto using_types = this->is_using_data_types();
		this->set_use_data_types(false);
		const auto read_count = this->read_uint32(&el_count);
		this->set_use_data_types(using_types);
		if (!read_count || (el_count == 0 && array_size != 0) ||
			(el_count != 0 && array_size % el_count != 0) || array_size > this->remaining_size())
		{
			return false;
		}

		if (element_count)
		{
			*element_count = el_count;
		}
		if (element_size)
		{
			*element_size = el_count ? array_size / el_count : 0;
		}

		return true;
	}

	bool byte_buffer::write_bool(bool data)
	{
		this->write_data_type(BD_BB_BOOL_TYPE);
		return this->write(sizeof(data), &data);
	}

	bool byte_buffer::write_byte(char data)
	{
		this->write_data_type(BD_BB_SIGNED_CHAR8_TYPE);
		return this->write(sizeof(data), &data);
	}

	bool byte_buffer::write_ubyte(unsigned char data)
	{
		this->write_data_type(BD_BB_UNSIGNED_CHAR8_TYPE);
		return this->write(sizeof(data), &data);
	}

	bool byte_buffer::write_int16(short data)
	{
		this->write_data_type(BD_BB_SIGNED_INTEGER16_TYPE);
		return this->write(sizeof(data), &data);
	}

	bool byte_buffer::write_uint16(unsigned short data)
	{
		this->write_data_type(BD_BB_UNSIGNED_INTEGER16_TYPE);
		return this->write(sizeof(data), &data);
	}

	bool byte_buffer::write_int32(int data)
	{
		this->write_data_type(BD_BB_SIGNED_INTEGER32_TYPE);
		return this->write(sizeof(data), &data);
	}

	bool byte_buffer::write_uint32(unsigned int data)
	{
		this->write_data_type(BD_BB_UNSIGNED_INTEGER32_TYPE);
		return this->write(sizeof(data), &data);
	}

	bool byte_buffer::write_int64(__int64 data)
	{
		this->write_data_type(BD_BB_SIGNED_INTEGER64_TYPE);
		return this->write(sizeof(data), &data);
	}

	bool byte_buffer::write_uint64(unsigned __int64 data)
	{
		this->write_data_type(BD_BB_UNSIGNED_INTEGER64_TYPE);
		return this->write(sizeof(data), &data);
	}

	bool byte_buffer::write_data_type(unsigned char data)
	{
		if (!this->use_data_types_) return true;
		return this->write(sizeof(data), &data);
	}

	bool byte_buffer::write_float(float data)
	{
		this->write_data_type(BD_BB_FLOAT32_TYPE);
		return this->write(sizeof(data), &data);
	}

	bool byte_buffer::write_string(const std::string& data)
	{
		return this->write_string(data.data());
	}

	bool byte_buffer::write_string(const char* data)
	{
		this->write_data_type(BD_BB_SIGNED_CHAR8_STRING_TYPE);
		return this->write(static_cast<int>(strlen(data)) + 1, data);
	}

	bool byte_buffer::write_blob(const std::string& data)
	{
		return this->write_blob(data.data(), INT(data.size()));
	}

	bool byte_buffer::write_blob(const char* data, const int length)
	{
		this->write_data_type(BD_BB_BLOB_TYPE);
		this->write_uint32(length);

		return this->write(length, data);
	}

	bool byte_buffer::write_struct(void* data, const int length)
	{
		this->write_data_type(BD_BB_STRUCTURED_DATA_TYPE);
		this->write_uint32(length);

		return this->write(length, data);
	}

	bool byte_buffer::write_array_header(const unsigned char type, const unsigned int element_count,
	                                     const unsigned int element_size)
	{
		const auto using_types = this->is_using_data_types();
		this->set_use_data_types(false);

		auto result = this->write_ubyte(type + 100);

		this->set_use_data_types(true);
		result &= this->write_uint32(element_count * element_size);
		this->set_use_data_types(false);

		result &= this->write_uint32(element_count);

		this->set_use_data_types(using_types);
		return result;
	}

	bool byte_buffer::read(const int bytes, void* output)
	{
		if (bytes < 0 || !output || this->current_byte_ > this->buffer_.size() ||
			static_cast<size_t>(bytes) > this->buffer_.size() - this->current_byte_)
		{
			return false;
		}

		std::memmove(output, this->buffer_.data() + this->current_byte_, bytes);
		this->current_byte_ += bytes;

		return true;
	}

	bool byte_buffer::write(const int bytes, const void* data)
	{
		if (bytes < 0 || (!data && bytes != 0))
		{
			return false;
		}

		this->buffer_.append(static_cast<const char*>(data), bytes);
		this->current_byte_ += bytes;
		return true;
	}

	bool byte_buffer::write(const std::string& data)
	{
		return this->write(static_cast<int>(data.size()), data.data());
	}

	void byte_buffer::set_use_data_types(const bool use_data_types)
	{
		this->use_data_types_ = use_data_types;
	}

	size_t byte_buffer::size() const
	{
		return this->buffer_.size();
	}

	bool byte_buffer::is_using_data_types() const
	{
		return use_data_types_;
	}

	std::string& byte_buffer::get_buffer()
	{
		return this->buffer_;
	}

	std::string byte_buffer::get_remaining()
	{
		if (this->current_byte_ > this->buffer_.size())
		{
			return {};
		}

		return std::string(this->buffer_.begin() + this->current_byte_, this->buffer_.end());
	}

	size_t byte_buffer::remaining_size() const
	{
		return this->current_byte_ <= this->buffer_.size()
			? this->buffer_.size() - this->current_byte_
			: 0;
	}

	bool byte_buffer::has_only_zero_padding(const size_t maximum_size) const
	{
		if (this->current_byte_ > this->buffer_.size())
		{
			return false;
		}

		const auto remaining = this->remaining_size();
		if (remaining > maximum_size)
		{
			return false;
		}

		return std::all_of(this->buffer_.begin() + this->current_byte_, this->buffer_.end(),
			[](const char value)
			{
				return value == 0;
			});
	}

	bool byte_buffer::has_more_data() const
	{
		return this->buffer_.size() > this->current_byte_;
	}
}
