#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "console/console.hpp"
#include "fastfiles.hpp"

#include <utils/hook.hpp>

namespace weapon
{
	namespace
	{
		utils::hook::detour g_set_weapon_configstrings_hook;

		std::unordered_set<unsigned short> get_mod_zone_indices()
		{
			std::unordered_set<unsigned short> indices;
			for (const auto& zone : fastfiles::get_loaded_zones())
			{
				if (fastfiles::is_mod_zone(zone))
				{
					indices.insert(zone.index);
				}
			}

			return indices;
		}

		std::vector<std::string> get_mod_weapons()
		{
			std::vector<std::string> weapons;

			const auto mod_zones = get_mod_zone_indices();
			if (mod_zones.empty())
			{
				return weapons;
			}

			fastfiles::enum_asset_entries(game::ASSET_TYPE_WEAPON, [&](game::XAssetEntry* entry)
			{
				if (!mod_zones.contains(entry->zoneIndex))
				{
					return;
				}

				if (const auto* name = game::DB_GetXAssetName(&entry->asset))
				{
					weapons.emplace_back(name);
				}
			}, true);

			std::ranges::sort(weapons);
			return weapons;
		}

		void register_mod_weapons()
		{
			const auto stock_count = *game::mp::bg_lastParsedWeaponIndex;
			const auto weapons = get_mod_weapons();

			for (const auto& name : weapons)
			{
				if (*game::mp::bg_lastParsedWeaponIndex + 1 >= 826) // bg_weaponCompleteDefs 0x8A9C660
				{
					console::warn("[Weapon] weapon limit reached at '%s'\n", name.data());
					return;
				}

				game::mp::BG_GetWeaponIndexForName(name.data(), game::mp::G_RegisterWeapon);
			}

			console::info("[Weapon] %zu mod weapons loaded (%u registered, %u added)\n", weapons.size(),
				*game::mp::bg_lastParsedWeaponIndex, *game::mp::bg_lastParsedWeaponIndex - stock_count);
		}

		void g_set_weapon_configstrings_stub()
		{
			register_mod_weapons();
			g_set_weapon_configstrings_hook.invoke<void>();
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::uses_multiplayer_binary())
			{
				return;
			}

			g_set_weapon_configstrings_hook.create(game::mp::G_SetWeaponConfigstrings, g_set_weapon_configstrings_stub);
		}
	};
}

REGISTER_COMPONENT(weapon::component)
