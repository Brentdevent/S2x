local ROW_TEXT_WIDGET_NAMES = {
	"HostName",
	"Status",
	"MapName",
	"Players",
	"Mode",
	"Ping"
}

local function get_browser_state( element )
	if not element then
		return nil
	end

	local scoped_data = LUI.FlowManager.GetScopedData( element )
	return scoped_data and scoped_data.s2xServerBrowser or nil
end

local function get_row_text_widgets( row )
	local text_widgets = {}
	for _, widget_name in ipairs( ROW_TEXT_WIDGET_NAMES ) do
		if row[widget_name] then
			table.insert( text_widgets, row[widget_name] )
		end
	end
	return text_widgets
end

local function set_row_text_style( row, font, font_size )
	for _, text_widget in ipairs( row.RowTextWidgets or {} ) do
		text_widget:setFont( font )
		text_widget:setFontSize( font_size, 0 )
	end
end

local function set_border_alpha( row, alpha )
	if row.Border then
		row.Border:setAlpha( alpha, 0 )
	end
end

local function record_row_focus( row )
	local state = get_browser_state( row )
	if state then
		state.lastFocusedElement = row.populated and "serverButton" or nil
	end
end

local function postload_server_row( row, _, properties )
	properties = properties or {}
	if properties.buttonText and row.HostName then
		row.HostName:setText( properties.buttonText, 0 )
	end

	row.RowTextWidgets = get_row_text_widgets( row )

	row:addEventHandler( "button_over", function ( element, event )
		if properties.buttonOverFunc then
			properties.buttonOverFunc( element, event )
		end
		set_row_text_style( row, FONTS.BodyBoldFont.Font, 30 )
		set_border_alpha( row, 1 )
		record_row_focus( row )
	end )

	row:addEventHandler( "button_over_disable", function ( element, event )
		if properties.buttonOverDisableFunc then
			properties.buttonOverDisableFunc( element, event )
		end
		set_row_text_style( row, FONTS.BodyFont.Font, 28 )
		set_border_alpha( row, 1 )
	end )

	row:addEventHandler( "button_disable", function ()
		set_row_text_style( row, FONTS.BodyFont.Font, 28 )
		set_border_alpha( row, 0 )
	end )

	row:addEventHandler( "button_up", function ()
		set_row_text_style( row, FONTS.BodyFont.Font, 28 )
		set_border_alpha( row, 0 )
	end )
end

return {
	PostLoadFunc = postload_server_row
}
