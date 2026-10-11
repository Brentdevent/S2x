#pragma once

#include "game/demonware/reward/event_relay.hpp"

namespace demonware
{
	class service_server;
}

namespace order_progress
{
	// Main thread, for the owning local XUID
	std::optional<std::uint64_t> receive(const demonware::reward_event_relay::batch& batch);
	void disconnect();

	// DW worker: prepare committed progress and publish completion with its cache-refresh page.
	void send_notifications(demonware::service_server* server, std::uint64_t user,
		const std::string& response);
}
