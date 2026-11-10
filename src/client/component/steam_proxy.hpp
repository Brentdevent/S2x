#pragma once
#include <utils/nt.hpp>
#include <mutex>

namespace steam
{
	class friends;
	class utils;
}

namespace steam_proxy
{
	// Keep the real Steam interfaces alive for the duration of each forwarded call.
	struct client_access
	{
		std::unique_lock<std::recursive_mutex> lock;
		steam::friends* friends;
		steam::utils* utils;
	};

	client_access access_client();
	void remember_friend(uint64_t id);
	void run_callbacks();
	void shutdown();

	const utils::nt::library& get_overlay_module();
	const char* get_player_name();

	void initialize();
}
