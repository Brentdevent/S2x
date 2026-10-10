local row_controller = require( "s2x_server_browser_row_uc" )
local builders = LUI.MenuBuilder.m_types_build

builders.s2x_server_browser_row = function ( _, properties )
	properties = properties or {}

	local bounds = {
		left = 0, right = 1170 * _1080p, top = 0, bottom = 40 * _1080p,
		leftAnchor = true, rightAnchor = false, topAnchor = true, bottomAnchor = false
	}

	local row = properties.disableInteractivity and LUI.UIElement.new( bounds ) or LUI.UIButton.new( bounds )
	row.id = "s2x_server_browser_row"

	if not properties.disableInteractivity then
		row:makeFocusable()
	end

	local background = LUI.UIImage.new()
	background:setAnchorsAndPosition( 0, 0, 0, 0, 0, 0, 0, 0, 0 )
	background:setRGBFromInt( 0x292B2B, 0 )
	background:setAlpha( 0.65, 0 )
	row:addElement( background )
	row.Background = background

	local columns = {
		{ "Favorite", 8, 34, "+" },
		{ "HostName", 42, 414, "SERVER NAME" },
		{ "MapName", 430, 642, "MAP" },
		{ "Mode", 658, 870, "MODE" },
		{ "Players", 878, 1000, "PLAYERS" },
		{ "Bots", 1008, 1080, "BOTS" },
		{ "Ping", 1088, 1162, "PING" }
	}

	for _, column in ipairs( columns ) do
		local text = LUI.UIText.new()
		text.id = column[1]
		text:setAnchorsAndPosition( 0, 1, 0, 0, column[2] * _1080p, column[3] * _1080p, 2 * _1080p, -2 * _1080p, 0 )
		text:setFont( FONTS.BodyFont.Font )
		text:setFontSize( 22, 0 )
		text:setRGBFromInt( SWATCHES.Menus.MenuOffWhite, 0 )
		text:setHorizontalAlignment( column[2] >= 878 and LUI.HorizontalAlignment.Center or LUI.HorizontalAlignment.Left )
		text:setVerticalAlignment( LUI.VerticalAlignment.Middle )
		text:setupAutoScaleText()

		if properties.fontIconSet then
			text:setFontIconSet( properties.fontIconSet )
		end

		text:setText( properties.disableInteractivity and column[4] or "", 0 )
		row:addElement( text )
		row[column[1]] = text
	end

	local border = LUI.MenuBuilder.BuildRegisteredType( "GenericBorderFrame", {
		controllerIndex = properties.controllerIndex, disableInteractivity = true
	} )
	border:setAnchorsAndPosition( 0, 0, 0, 0, 0, 0, 0, 0, 0 )
	border:setRGBFromInt( SWATCHES.Menus.MenuGold, 0 )
	border:setAlpha( 0, 0 )
	row:addElement( border )
	row.Border = border

	if properties.isBuildChild then
		row:addEventHandler( "grid_cell_empty", function ()
			row.populated = false
			row.server = nil
			row.m_inputDisabled = true
			row:hide()
		end )

		row:addEventHandler( "grid_cell_populated", function ()
			row:show()
		end )
	end

	row_controller.PostLoadFunc( row, properties.controllerIndex, properties )

	return row
end
