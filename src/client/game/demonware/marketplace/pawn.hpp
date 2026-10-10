#pragma once

#include "collection.hpp"
#include "pawn_catalog.hpp"

namespace demonware::marketplace_pawn
{
	std::uint32_t pawn(byte_buffer* buffer, std::uint64_t user_id,
		const pawn_catalog::catalog* catalog, std::vector<std::string>& updates);
	marketplace_collection::result convert(const marketplace_collection::request& input,
		std::uint64_t user_id, const pawn_catalog::catalog* catalog);
}
