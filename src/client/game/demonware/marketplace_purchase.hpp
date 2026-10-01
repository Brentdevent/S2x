#pragma once

#include "byte_buffer.hpp"
#include "data_types.hpp"
#include "marketplace_product.hpp"
#include "marketplace_sku.hpp"
#include "collection_catalog.hpp"

namespace demonware::marketplace_purchase
{
	struct request
	{
		std::string client_tx;
		std::uint64_t player_id{};
		std::uint32_t sku_id{};
	};

	// The stock Quartermaster sends one unit, no custom prices or coupons.
	bool parse_request(byte_buffer* buffer, request& output);

	class result final : public bdTaskResult
	{
	public:
		std::uint32_t error{};
		std::string wire;
		void serialize(byte_buffer* buffer) override;
	};

	result purchase(const request& input, std::uint64_t local_user_id,
		const marketplace_sku::result* sku, const marketplace_product::result* product,
		const collection_catalog::catalog* collections = nullptr);
}
