#pragma once

#ifndef S2X_UPDATE_SOURCE
#define S2X_UPDATE_SOURCE "https://updater.s2x.dev"
#endif

namespace updater::update_source
{
	// Scoped to the official hostname, including when S2X_UPDATE_SOURCE is overridden.
	inline constexpr auto fallback_connect_to = "updater.s2x.dev:443:49.12.1.118:443";
	inline constexpr auto manifest = S2X_UPDATE_SOURCE "/s2x.json";
	inline constexpr auto files = S2X_UPDATE_SOURCE "/s2x/";
	inline constexpr auto store_runtime_manifest = S2X_UPDATE_SOURCE "/store-runtime.json";
	inline constexpr auto store_runtime_files = S2X_UPDATE_SOURCE "/store-runtime/";
}
