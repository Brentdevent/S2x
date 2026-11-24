#pragma once

#include "game/game.hpp"

namespace fastfiles
{
	struct zone_info
	{
		unsigned short index;
		std::string name;
		int flags;
	};

	bool is_supported();

	bool exists(const std::string& zone);
	bool is_zone_loaded(const std::string& zone);
	bool try_load_zone(const std::string& zone, int alloc_flags);

	bool is_mod_zone(const zone_info& info);
	std::string get_zone_name(unsigned int index);
	std::vector<zone_info> get_loaded_zones();

	void enum_asset_entries(game::XAssetType type, const std::function<void(game::XAssetEntry*)>& callback,
		bool include_override);
	void get_asset_counts(int (&counts)[game::ASSET_TYPE_COUNT]);
	int get_pool_size(game::XAssetType type);

	void reload_mod_zone(const std::function<void()>& on_loaded);
}
