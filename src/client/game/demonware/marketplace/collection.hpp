#pragma once

#include "game/demonware/byte_buffer.hpp"
#include "collection_catalog.hpp"
#include "game/demonware/data_types.hpp"
#include "store.hpp"

namespace demonware::marketplace_collection
{
	struct request
	{
		std::string rule;
		std::string client_tx;
		std::uint32_t quantity{1};
	};

	bool parse_request(byte_buffer* buffer, request& output);

	class result final : public bdTaskResult
	{
	public:
		std::uint32_t error{};
		std::string wire;
		void serialize(byte_buffer* buffer) override;
	};

	result redeem(const request& input, std::uint64_t user_id, const collection_catalog::catalog* catalog);
	result refresh_response(const std::string& receipt_json, std::uint64_t user_id);
	std::string inventory_update(const marketplace_store::inventory_record& item, std::uint64_t user_id);
	std::string balance_update(std::uint8_t currency, std::uint32_t balance, std::uint64_t user_id);
}
