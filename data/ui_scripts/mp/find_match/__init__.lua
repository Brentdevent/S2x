if game:issingleplayer() or not Engine.InFrontend() then
	return
end

print("[S2x] Loading Find Match patch")
print("[S2x] Note: using raw strings because S2x currently has no game:addlocalizedstring helper")

-- This follows the S1x ui_scripts pattern: load a local copy of the menu builder
-- and let it overwrite LUI.MenuBuilder.m_types_build["findgame_menu"].
-- The dedicated browser uses S2x-only menu names so stock System Link stays intact.
package.loaded["findgame_menu"] = nil
package.loaded["findgame_menu_uc"] = nil
package.loaded["s2x_server_browser"] = nil
package.loaded["s2x_server_browser_uc"] = nil
package.loaded["s2x_server_browser_row"] = nil
package.loaded["s2x_server_browser_row_uc"] = nil

require("s2x_server_browser_row")
require("s2x_server_browser")
require("findgame_menu")
