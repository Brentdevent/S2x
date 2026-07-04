#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "console/console.hpp"

#include <utils/hook.hpp>

namespace patches
{
	namespace
	{
		utils::hook::detour clear_hub_vendor_targets_hook;

		void register_hub_vendors()
		{
			game::CG_RegisterHubVendorTarget(415, "quartermaster_menu");
			game::CG_RegisterHubVendorTarget(413, "headquarters_menu");
			game::CG_RegisterHubVendorTarget(377, "blacksmith_menu");
			game::CG_RegisterHubVendorTarget(427, "mail_officer_menu");
			game::CG_RegisterHubVendorTarget(414, "zombie_officer_menu");
			game::CG_RegisterHubVendorTarget(401, "general_menu");
			game::CG_RegisterHubVendorTarget(407, "onevone_officer_menu");
			game::CG_RegisterHubVendorTarget(510, "theater_menu");
			game::CG_RegisterHubVendorTarget(396, "scorestreak_officer_menu");
			game::CG_RegisterHubVendorTarget(426, "company_commander_menu");
			game::CG_RegisterHubVendorTarget(369, "tdm_playlist_menu");
			game::CG_RegisterHubVendorTarget(368, "dom_playlist_menu");
			game::CG_RegisterHubVendorTarget(370, "war_playlist_menu");
			game::CG_RegisterHubVendorTarget(397, "r_and_r_menu");
			game::CG_RegisterHubVendorTarget(402, "overlook_menu");
			game::CG_RegisterHubVendorTarget(403, "division_prestige_menu");

			console::info("[hub] registered vendor targets\n");
		}

		bool is_full_hub()
		{
			const auto* mapname = game::Dvar_FindMalleableVar("mapname");
			return mapname && mapname->current.string &&
				   mapname->current.string == "mp_hub_allies"s;
		}

		void clear_hub_vendor_targets_stub()
		{
			clear_hub_vendor_targets_hook.invoke();

			if (is_full_hub())
			{
				register_hub_vendors();
			}
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_thread_setup() override
		{
#ifdef DEBUG
			// Allow multiple instances for testing purposes.
			utils::hook::set(0x78A5F0_g, 0xC301B0);
#endif
		}

		void post_unpack() override
		{
			// Temp fix for vendor interactions in the Hub 
			// Normally this seems to get set from GSC, but it doesn't for some reason
			clear_hub_vendor_targets_hook.create(0x4D9D0_g, clear_hub_vendor_targets_stub);

			// Show HQ tab in menu
			game::Dvar_RegisterBoolProtected("allow_hub_vendor_menu", true, game::DVAR_FLAG_NONE);
			// Alternate hub flow
			game::Dvar_RegisterBoolProtected("2917", true, game::DVAR_FLAG_NONE); 
			// Full hub flow false
			game::Dvar_RegisterBool("2464", false, game::DVAR_FLAG_NONE);         
			// vlobby enabled
			game::Dvar_RegisterBool("4287", true, game::DVAR_FLAG_NONE);          
			// Skip intro's
			game::Dvar_RegisterBool("2665", true, game::DVAR_FLAG_NONE);          
		}
	};
}

REGISTER_COMPONENT(patches::component)
