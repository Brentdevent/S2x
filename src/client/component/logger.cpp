#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include "console/console.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>
#include <utils/concurrency.hpp>

namespace logger
{
	namespace
	{
		utils::hook::detour db_unload_x_zones_hook;
		utils::hook::detour db_try_load_x_file_internal_hook;

		struct zone_info
		{
			const char* name;
			int flags;
		};

		zone_info get_zone_info(const uint16_t zone_index)
		{
			if (!game::environment::uses_multiplayer_binary())
			{
				const auto zone = reinterpret_cast<game::sp::XZone*>(
					&game::g_zones[sizeof(game::sp::XZone) * zone_index]
				);

				return { zone->name, zone->flags };
			}

			const auto zone = reinterpret_cast<game::mp::XZone*>(
				&game::g_zones[sizeof(game::mp::XZone) * zone_index]
			);

			return { zone->name, zone->flags };
		}

		void db_unload_x_zones_stub(uint16_t* zone_indices, unsigned int zone_count, bool create_default)
		{
			for (unsigned int i = 0; i < zone_count; i++)
			{
				const auto zone_index = zone_indices[i];	
				const auto zone_info = get_zone_info(zone_index);

				if (zone_info.name && *zone_info.name)
				{
					console::info("Unloading zone %s\n", zone_info.name);
				}
				else
				{
					console::info("Unloading zone <unknown>\n");
				}

				if (dvars::db_verboseLogging && dvars::db_verboseLogging->current.enabled)
				{
					console::info("Unloading details: index=%u flags=0x%X createDefault=%d\n",
						zone_index,
						zone_info.flags,
						create_default);
				}
			}
					
			db_unload_x_zones_hook.invoke<void>(zone_indices, zone_count, create_default);
		}

		int db_try_load_x_file_internal(const char* zone_name, int zone_flags, int is_base_map)
		{
			console::info("Loading zone %s\n", zone_name && *zone_name ? zone_name : "<unknown>");
			
			if (dvars::db_verboseLogging && dvars::db_verboseLogging->current.enabled)
			{
				console::info("Loading details: flags=0x%X base=%d\n", zone_flags, is_base_map);
			}

			return db_try_load_x_file_internal_hook.invoke<int>(zone_name, zone_flags, is_base_map);
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			dvars::db_verboseLogging = game::Dvar_RegisterBool("db_verboseLogging", false, game::DVAR_FLAG_SAVED);

			db_unload_x_zones_hook.create(game::DB_UnloadXZones, db_unload_x_zones_stub);
			db_try_load_x_file_internal_hook.create(game::DB_TryLoadXFileInternal, db_try_load_x_file_internal);
		}
	};
}

REGISTER_COMPONENT(logger::component)
