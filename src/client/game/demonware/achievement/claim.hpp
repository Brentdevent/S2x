#pragma once

#include "store.hpp"

namespace demonware
{
	class byte_buffer;
	class service_server;
}

namespace demonware::achievement_claim
{
	bool settle_reward(marketplace_store::transaction& economy, const achievement_record& record, bool grant,
		std::uint64_t user, std::uint32_t timestamp, std::string& response);

	bool try_handle(service_server* server, byte_buffer* buffer);
}
