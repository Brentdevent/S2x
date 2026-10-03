#include <std_include.hpp>

#include "marketplace_product.hpp"

#include <algorithm>
#include <utility>

namespace demonware::marketplace_product
{
	bool parse_request(byte_buffer* buffer, request& output)
	{
		request parsed{};
		std::uint32_t product_count{};
		if (!buffer || !buffer->read_string(&parsed.context, maximum_context_length) ||
			!buffer->read_uint32(&parsed.page) ||
			!buffer->read_uint32(&parsed.maximum_results) ||
			!buffer->read_uint32(&product_count) || parsed.context != "s2_steam" ||
			parsed.page != 1 || parsed.maximum_results == 0 ||
			parsed.maximum_results > maximum_product_results || product_count == 0 ||
			product_count > maximum_product_results ||
			parsed.maximum_results != product_count)
		{
			return false;
		}

		parsed.product_ids.reserve(product_count);
		for (std::uint32_t index = 0; index < product_count; ++index)
		{
			std::uint32_t product_id{};
			if (!buffer->read_uint32(&product_id) || product_id == 0 ||
				std::find(parsed.product_ids.begin(), parsed.product_ids.end(), product_id) !=
				parsed.product_ids.end())
			{
				return false;
			}
			parsed.product_ids.push_back(product_id);
		}

		if (!buffer->has_only_zero_padding(maximum_zero_padding))
		{
			return false;
		}

		output = std::move(parsed);
		return true;
	}

	bool result::valid() const
	{
		return blob_1.size() <= maximum_blob_1_length &&
			blob_2.size() <= maximum_blob_2_length &&
			blob_3.size() <= maximum_blob_3_length && items.size() <= maximum_items &&
			pairs_1.size() <= maximum_pairs && pairs_2.size() <= maximum_pairs;
	}

	void result::serialize(byte_buffer* buffer)
	{
		if (!buffer || !valid())
		{
			return;
		}

		buffer->write_uint32(product_id);
		buffer->write_blob(blob_1);
		buffer->write_blob(blob_2);
		buffer->write_blob(blob_3);
		buffer->write_uint16(field_20);
		buffer->write_uint32(field_24);
		serialize_pairs(buffer, items);
		serialize_pairs(buffer, pairs_1);
		serialize_pairs(buffer, pairs_2);
	}

	void result::serialize_pairs(byte_buffer* buffer, const std::vector<pair>& values)
	{
		buffer->write_uint32(static_cast<std::uint32_t>(values.size()));
		for (const auto& value : values)
		{
			buffer->write_uint32(value.first);
			buffer->write_uint32(value.second);
		}
	}

	bool parse_result(const std::string_view raw, result& output)
	{
		if (raw.empty() || raw.size() > 16384)
		{
			return false;
		}

		byte_buffer buffer{std::string{raw}};
		result parsed{};
		if (!buffer.read_uint32(&parsed.product_id) || parsed.product_id == 0 ||
			!buffer.read_blob(&parsed.blob_1, maximum_blob_1_length) ||
			!buffer.read_blob(&parsed.blob_2, maximum_blob_2_length) ||
			!buffer.read_blob(&parsed.blob_3, maximum_blob_3_length) ||
			!buffer.read_uint16(&parsed.field_20) ||
			!buffer.read_uint32(&parsed.field_24))
		{
			return false;
		}

		const auto parse_pairs = [&buffer](std::vector<pair>& output_pairs,
			const std::size_t maximum_count)
		{
			std::uint32_t count{};
			if (!buffer.read_uint32(&count) || count > maximum_count)
			{
				return false;
			}

			output_pairs.reserve(count);
			for (std::uint32_t index = 0; index < count; ++index)
			{
				pair entry{};
				if (!buffer.read_uint32(&entry.first) ||
					!buffer.read_uint32(&entry.second))
				{
					return false;
				}
				output_pairs.emplace_back(entry);
			}
			return true;
		};

		if (!parse_pairs(parsed.items, maximum_items) ||
			!parse_pairs(parsed.pairs_1, maximum_pairs) ||
			!parse_pairs(parsed.pairs_2, maximum_pairs) ||
			buffer.remaining_size() != 0 || !parsed.valid())
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
