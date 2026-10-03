#pragma once

#include "byte_buffer.hpp"
#include "data_types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace demonware::marketplace_product
{
	constexpr std::size_t maximum_context_length = 16;
	constexpr std::size_t maximum_zero_padding = 15;
	constexpr std::uint32_t maximum_product_results = 100;
	constexpr std::size_t maximum_blob_1_length = 135;
	constexpr std::size_t maximum_blob_2_length = 240;
	constexpr std::size_t maximum_blob_3_length = 64;
	constexpr std::size_t maximum_items = 10;
	constexpr std::size_t maximum_pairs = 4;

	struct request
	{
		std::string context;
		std::uint32_t page{};
		std::uint32_t maximum_results{};
		std::vector<std::uint32_t> product_ids;
	};

	bool parse_request(byte_buffer* buffer, request& output);

	struct pair
	{
		std::uint32_t first{};
		std::uint32_t second{};
	};

	class result final : public bdTaskResult
	{
	public:
		result() = default;

		bool valid() const;

		void serialize(byte_buffer* buffer) override;

		std::uint32_t product_id{};
		std::string blob_1;
		std::string blob_2;
		std::string blob_3;
		std::uint16_t field_20{};
		std::uint32_t field_24{};
		std::vector<pair> items;
		std::vector<pair> pairs_1;
		std::vector<pair> pairs_2;

	private:
		static void serialize_pairs(byte_buffer* buffer, const std::vector<pair>& values);
	};

	bool parse_result(const std::string_view raw, result& output);
}
