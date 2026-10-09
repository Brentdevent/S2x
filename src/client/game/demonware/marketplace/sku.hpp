#pragma once

#include "game/demonware/byte_buffer.hpp"
#include "game/demonware/data_types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace demonware::marketplace_sku
{
	constexpr std::size_t maximum_context_length = 16;
	constexpr std::size_t maximum_zero_padding = 15;
	constexpr std::uint32_t stock_page_size = 100;
	constexpr std::uint8_t quartermaster_sku_type = 100;
	constexpr std::uint8_t collection_sku_type = 150;
	constexpr std::size_t maximum_sku_data_length = 64;
	constexpr std::size_t maximum_promotional_text_length = 135;
	constexpr std::size_t maximum_prices = 10;

	struct request
	{
		std::string context;
		std::uint32_t page{};
		std::uint32_t page_size{};
		bool field_bool{};
		std::uint32_t field_count_1{};
		std::uint32_t field_count_2{};
		std::uint8_t sku_type{};
		std::string terminal_string;
	};

	bool parse_request(byte_buffer* buffer, request& output);

	struct price
	{
		std::uint8_t currency{};
		std::uint32_t value{};
	};

	class result final : public bdTaskResult
	{
	public:
		result() = default;

		bool valid() const;

		void serialize(byte_buffer* buffer) override;

		std::uint32_t sku_id{};
		std::uint32_t field_36{};
		std::uint8_t field_40{1};
		std::string sku_data;
		std::uint8_t field_106{};
		std::uint8_t field_107{1};
		std::uint32_t field_108{};
		std::uint32_t field_112{};
		std::uint32_t field_116{};
		std::uint8_t field_120{2};
		std::string promotional_text;
		std::uint32_t field_268{};
		std::uint16_t field_272{};
		std::uint32_t field_276{};
		std::vector<price> prices;
		std::uint8_t sku_type{quartermaster_sku_type};
		std::uint32_t maximum_quantity{0xFFFFFFFF};
		bool sold_out{};
	};

	bool parse_result(const std::string_view raw, result& output);
}
