if game:issingleplayer() or not Engine.InFrontend() then
	return
end

local MODULES_TO_RELOAD = {
	"s2x_findgame_menu",
	"s2x_server_browser_row_uc",
	"s2x_server_browser_row",
	"s2x_server_browser_uc",
	"s2x_server_browser"
}

for _, module_name in ipairs( MODULES_TO_RELOAD ) do
	package.loaded[module_name] = nil
end

local browser_controller = require( "s2x_server_browser_uc" )
require( "s2x_server_browser_row" )
require( "s2x_server_browser" )

-- Wrap the packaged UC table instead of maintaining a decompiled copy. The
-- wrapper applies the S2x Find Match layouts to the stock controller state.
local findgame_patch = require( "s2x_findgame_menu" )
local menu_builders = LUI and LUI.MenuBuilder and LUI.MenuBuilder.m_types_build or nil
assert( type( menu_builders ) == "table", "Missing LUI menu builder registry" )

local previous_stock_controller = package.loaded["s2.findgame_menu_uc"]
package.loaded["s2.findgame_menu_uc"] = nil

local controller_loaded, stock_findgame_controller =
	pcall( require, "s2.findgame_menu_uc" )
if not controller_loaded or type( stock_findgame_controller ) ~= "table" then
	package.loaded["s2.findgame_menu_uc"] = previous_stock_controller
	local failure_message = not controller_loaded and stock_findgame_controller or
		"Stock findgame_menu_uc did not return a table"
	error( failure_message )
end

local wrapped, wrap_error = pcall(
	findgame_patch.wrap_stock_controller,
	stock_findgame_controller,
	browser_controller.S2X_LABELS
)
if not wrapped then
	package.loaded["s2.findgame_menu_uc"] = previous_stock_controller
	error( wrap_error )
end

-- The stock builder captures its UC functions in locals when required. Remove
-- the existing registered builder, then reload it so it captures the wrappers.
local previous_findgame_builder = menu_builders["findgame_menu"]
local previous_findgame_module = package.loaded["s2.findgame_menu"]
menu_builders["findgame_menu"] = nil
package.loaded["s2.findgame_menu"] = nil

local loaded, load_error = pcall( require, "s2.findgame_menu" )
if not loaded or type( menu_builders["findgame_menu"] ) ~= "function" then
	menu_builders["findgame_menu"] = previous_findgame_builder
	package.loaded["s2.findgame_menu"] = previous_findgame_module
	package.loaded["s2.findgame_menu_uc"] = previous_stock_controller
	local failure_message = not loaded and load_error or
		"Stock findgame_menu did not register a builder"
	error( failure_message )
end
