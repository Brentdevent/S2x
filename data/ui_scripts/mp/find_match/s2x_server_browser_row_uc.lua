local browser = require( "s2x_server_browser_uc" )
local columns = { "Favorite", "HostName", "MapName", "Mode", "Players", "Bots", "Ping" }

local function postload_server_row( row, _, properties )
	if properties.disableInteractivity then
		return
	end

	local function style( focused )
		for _, name in ipairs( columns ) do
			row[name]:setFont( focused and FONTS.BodyBoldFont.Font or FONTS.BodyFont.Font )
			row[name]:setRGBFromInt( focused and SWATCHES.Menus.MenuGold or SWATCHES.Menus.MenuOffWhite, 0 )
		end

		row.Border:setAlpha( focused and 1 or 0, 0 )
	end

	row:addEventHandler( "button_over", function ()
		style( true )
		browser.select_row( row )
	end )

	row:addEventHandler( "button_up", function ()
		style( false )
	end )

	row:addEventHandler( "button_disable", function ()
		style( false )
	end )

	row:addEventHandler( "button_action", function ()
		browser.select_row( row )

		local scoped = LUI.FlowManager.GetScopedData( row )

		if row.populated and scoped and scoped.s2xServerBrowser then
			browser.join_selected_server( scoped.s2xServerBrowser.menu )
		end
	end )
end

return { PostLoadFunc = postload_server_row }
