#include <std_include.hpp>

#include "sku.hpp"

#include <utility>

namespace demonware::marketplace_sku
{
	bool parse_request(byte_buffer* buffer, request& output)
	{
		request parsed{};
		unsigned char sku_type{};
		if (!buffer || !buffer->read_string(&parsed.context, maximum_context_length) ||
			!buffer->read_uint32(&parsed.page) ||
			!buffer->read_uint32(&parsed.page_size) ||
			!buffer->read_bool(&parsed.field_bool) ||
			!buffer->read_uint32(&parsed.field_count_1) ||
			!buffer->read_uint32(&parsed.field_count_2) ||
			!buffer->read_ubyte(&sku_type) ||
			!buffer->read_string(&parsed.terminal_string, 0) ||
			!buffer->has_only_zero_padding(maximum_zero_padding))
		{
			return false;
		}

		parsed.sku_type = sku_type;
		if (parsed.context != "s2_steam" || parsed.page == 0 ||
			parsed.page_size != stock_page_size || parsed.field_bool ||
			parsed.field_count_1 != 0 || parsed.field_count_2 != 1 ||
			!parsed.terminal_string.empty())
		{
			return false;
		}

		output = std::move(parsed);
		return true;
	}

	bool result::valid() const
	{
		return sku_data.size() <= maximum_sku_data_length &&
			promotional_text.size() <= maximum_promotional_text_length &&
			prices.size() <= maximum_prices;
	}

	void result::serialize(byte_buffer* buffer)
	{
		if (!buffer || !valid())
		{
			return;
		}

		buffer->write_uint32(sku_id);
		buffer->write_uint32(field_36);
		buffer->write_ubyte(field_40);
		buffer->write_blob(sku_data);
		buffer->write_ubyte(field_106);
		buffer->write_uint32(field_108);
		buffer->write_uint32(field_112);
		buffer->write_uint32(field_116);
		buffer->write_ubyte(field_120);
		buffer->write_blob(promotional_text);
		buffer->write_uint32(field_268);
		buffer->write_uint16(field_272);
		buffer->write_uint32(field_276);
		buffer->write_ubyte(field_107);
		buffer->write_uint32(static_cast<std::uint32_t>(prices.size()));
		for (const auto& entry : prices)
		{
			buffer->write_ubyte(entry.currency);
			buffer->write_uint32(entry.value);
		}
		buffer->write_ubyte(sku_type);
		buffer->write_uint32(maximum_quantity);
		buffer->write_bool(sold_out);
	}

	bool parse_result(const std::string_view raw, result& output)
	{
		if (raw.empty() || raw.size() > 4096)
		{
			return false;
		}

		byte_buffer buffer{std::string{raw}};
		result parsed{};
		std::uint32_t price_count{};
		if (!buffer.read_uint32(&parsed.sku_id) || parsed.sku_id == 0 ||
			!buffer.read_uint32(&parsed.field_36) ||
			!buffer.read_ubyte(&parsed.field_40) ||
			!buffer.read_blob(&parsed.sku_data, maximum_sku_data_length) ||
			!buffer.read_ubyte(&parsed.field_106) ||
			!buffer.read_uint32(&parsed.field_108) ||
			!buffer.read_uint32(&parsed.field_112) ||
			!buffer.read_uint32(&parsed.field_116) ||
			!buffer.read_ubyte(&parsed.field_120) ||
			!buffer.read_blob(&parsed.promotional_text, maximum_promotional_text_length) ||
			!buffer.read_uint32(&parsed.field_268) ||
			!buffer.read_uint16(&parsed.field_272) ||
			!buffer.read_uint32(&parsed.field_276) ||
			!buffer.read_ubyte(&parsed.field_107) ||
			!buffer.read_uint32(&price_count) || price_count > maximum_prices)
		{
			return false;
		}

		parsed.prices.reserve(price_count);
		for (std::uint32_t index = 0; index < price_count; ++index)
		{
			price entry{};
			if (!buffer.read_ubyte(&entry.currency) || !buffer.read_uint32(&entry.value))
			{
				return false;
			}

			parsed.prices.emplace_back(entry);
		}

		if (!buffer.read_ubyte(&parsed.sku_type) ||
			!buffer.read_uint32(&parsed.maximum_quantity) ||
			!buffer.read_bool(&parsed.sold_out) || buffer.remaining_size() != 0 ||
			!parsed.valid())
		{
			return false;
		}

		byte_buffer round_trip{};
		parsed.serialize(&round_trip);
		if (round_trip.get_buffer() != raw)
		{
			return false;
		}

		output = std::move(parsed);
		return true;
	}
}
