#pragma once

#ifndef S2X_UPDATE_SOURCE
#define S2X_UPDATE_SOURCE "https://updater.s2x.dev"
#endif

namespace updater::update_source
{
	inline constexpr auto manifest = S2X_UPDATE_SOURCE "/s2x.json";
	inline constexpr auto files = S2X_UPDATE_SOURCE "/s2x/";
	inline constexpr auto store_runtime_manifest = S2X_UPDATE_SOURCE "/store-runtime.json";
	inline constexpr auto store_runtime_files = S2X_UPDATE_SOURCE "/store-runtime/";
}
