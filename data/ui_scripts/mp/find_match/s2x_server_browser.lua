local browser = require( "s2x_server_browser_uc" )
local builders = LUI.MenuBuilder.m_types_build

-- Coordinates are in the game's 1080p UI space, using the stock menu fonts,
-- gold focus treatment, GenericButton controls and controller navigation.
local function position( element, left, right, top, bottom )
	element:setAnchorsAndPosition( 0, 1, 0, 1, left * _1080p, right * _1080p, top * _1080p, bottom * _1080p, 0 )
end

local function add_text( menu, properties, id, left, right, top, bottom, size, text, gold )
	local element = LUI.UIText.new()
	element.id = id
	position( element, left, right, top, bottom )
	element:setFont( FONTS.BodyFont.Font )
	element:setFontSize( size, 0 )
	element:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
	element:setVerticalAlignment( LUI.VerticalAlignment.Middle )
	element:setRGBFromInt( gold and SWATCHES.Menus.MenuGold or SWATCHES.Menus.MenuOffWhite, 0 )

	if properties.fontIconSet then
		element:setFontIconSet( properties.fontIconSet )
	end

	element:setText( text or "", 0 )
	menu:addElement( element )
	menu[id] = element

	return element
end

local function add_image( menu, id, left, right, top, bottom, color, alpha )
	local image = LUI.UIImage.new()
	image.id = id
	position( image, left, right, top, bottom )
	image:setRGBFromInt( color, 0 )
	image:setAlpha( alpha, 0 )
	menu:addElement( image )
	menu[id] = image

	return image
end

local function add_button( menu, properties, id, left, right, top, text, description, action )
	local button = LUI.MenuBuilder.BuildRegisteredType( "GenericButton", {
		controllerIndex = properties.controllerIndex, fontIconSet = properties.fontIconSet,
		buttonText = text, desc_text = description, useSmallBorder = true
	} )
	button.id = id
	position( button, left, right, top, top + 48 )

	if button.Name then
		button.Name:setupAutoScaleText()
	end

	menu:addElement( button )
	menu[id] = button

	button:addEventHandler( "button_action", function ()
		action( menu )
	end )

	button:addEventHandler( "gain_focus", function ()
		browser.record_focus( menu, button )
	end )

	return button
end

builders.s2x_server_browser = function ( _, properties )
	properties = properties or {}

	local menu = LUI.UIGenericNavigator.new( {
		left = 0, right = 0, top = 0, bottom = 0,
		leftAnchor = true, rightAnchor = true, topAnchor = true, bottomAnchor = true
	} )
	menu.id = properties.headquarters and "s2x_hq_browser" or "s2x_server_browser"

	local controller = properties.controllerIndex

	if not controller then
		controller = Engine.InFrontend() and LUI.FlowManager.GetScopedData( menu ).exclusiveControllerIndex or menu:getRootController()
	end

	properties.controllerIndex = controller
	browser.PreLoadFunc( menu, controller, properties )

	if CONDITIONS.IsE3Build() then
		browser.setup_for_e3( controller )
	end

	menu:playSound( "menu_open" )

	local title = LUI.MenuBuilder.BuildRegisteredType( "GenericMenuTitle", properties )
	position( title, 100, 1790, 110, 158 )

	if title.Title then
		title.Title:setText( properties.headquarters and browser.S2X_LABELS.HQ_BROWSER or browser.S2X_LABELS.SERVER_BROWSER, 0 )
		title.Title:setFont( FONTS.BodyBoldFont.Font )
	end

	menu:addElement( title )
	add_text( menu, properties, "MenuDescription", 100, 1790, 164, 192, 20,
		properties.headquarters and browser.S2X_LABELS.HQ_BROWSER_DESCRIPTION or browser.S2X_LABELS.SERVER_BROWSER_DESCRIPTION )

	add_button( menu, properties, "SourceButton", 100, 445, 212, "SOURCE: INTERNET", "Switch between Internet servers and your saved favorites.",
		function ()
			browser.change_option( menu, "source" )
		end )

	add_button( menu, properties, "FilterButton", 460, 875, 212, "SHOW: ALL SERVERS", "Show all servers, servers with human players, or servers with open slots.",
		function ()
			browser.change_option( menu, "filter" )
		end )

	add_button( menu, properties, "SortButton", 890, 1270, 212, "SORT: PLAYERS",
		properties.headquarters and "Sort by players, ping, or server name." or "Sort by human players, ping, server name, or map.",
		function ()
			browser.change_option( menu, "sort" )
		end )

	add_button( menu, properties, "RefreshListButton", 1330, 1790, 212, "REFRESH", "Query Internet servers and saved favorites.", browser.refresh_server_list )

	local header = LUI.MenuBuilder.BuildRegisteredType( "s2x_server_browser_row", {
		controllerIndex = controller, fontIconSet = properties.fontIconSet, disableInteractivity = true,
		headquarters = properties.headquarters
	} )
	position( header, 100, 1270, 288, 328 )
	menu:addElement( header )
	menu.Header = header

	local indicator = LUI.UIGridPositionIndicator.new( { controllerIndex = controller, fontIconSet = properties.fontIconSet, hasCarots = true } )
	position( indicator, 1110, 1270, 902, 932 )
	indicator:setFont( FONTS.BodyBoldFont.Font )
	indicator:setFontSize( 20, 0 )
	indicator:setHorizontalAlignment( LUI.HorizontalAlignment.Right )
	indicator:setRGBFromInt( SWATCHES.Menus.MenuOffWhite, 0 )
	menu:addElement( indicator )
	menu.GridPositionIndicator = indicator

	local grid = LUI.UIGridIW7.new( nil, {
		controllerIndex = controller, fontIconSet = properties.fontIconSet,
		buildChild = function ()
			return LUI.MenuBuilder.BuildRegisteredType( "s2x_server_browser_row", {
				controllerIndex = controller, fontIconSet = properties.fontIconSet, isBuildChild = true,
				headquarters = properties.headquarters
			} )
		end,
		columnWidth = 1170 * _1080p, rowHeight = 40 * _1080p,
		horizontalAlignment = LUI.Alignment.Left, horizontalAnchor = LUI.UIGrid.AnchorType.Origin,
		horizontalScrollType = LUI.ScrollType.AnchoredEdge,
		verticalAlignment = LUI.Alignment.Top, verticalAnchor = LUI.UIGrid.AnchorType.Origin,
		verticalScrollType = LUI.ScrollType.AnchoredEdge,
		isPositionFocusable = browser.can_focus_list_item,
		refreshChild = browser.populate_server_row,
		maxVisibleColumns = 1, maxVisibleRows = 14, spacingX = 0, spacingY = 0,
		wrapX = false, wrapY = false
	} )
	grid.id = "AvailableGames"
	position( grid, 100, 1270, 332, 892 )
	grid:SetPositionIndicator( indicator )
	menu:addElement( grid )
	menu.AvailableGames = grid

	grid:addEventHandler( "gain_focus", function ()
		browser.record_focus( menu, grid )
	end )

	local scrollbar = LUI.UIVerticalScrollbar.new( nil, { controllerIndex = controller, scrollbarWidth = 3 } )
	position( scrollbar, 1280, 1300, 332, 892 )
	scrollbar:setBarAlpha( 0.5 )
	scrollbar:setTrackAlpha( 0.1 )
	scrollbar:setBarColor( 0xFFFFFF, 0 )
	scrollbar:setTrackColor( 0xFFFFFF, 0 )
	menu:addElement( scrollbar )
	menu.ScrollBar = scrollbar
	grid:SetScrollbar( scrollbar )

	add_text( menu, properties, "EmptyState", 120, 1250, 380, 460, 26, "Searching for servers..." )

	add_image( menu, "DetailBackground", 1330, 1790, 288, properties.headquarters and 828 or 892, 0x161A1B, 0.85 )
	add_text( menu, properties, "DetailHost", 1346, 1774, 298, 344, 28, "SELECT A SERVER", true ):setupAutoScaleText()
	add_image( menu, "PreviewBackground", 1346, 1774, 356, 597, 0x292B2B, 1 )
	add_image( menu, "MapPreview", 1346, 1774, 356, 597, 0xFFFFFF, 0 )

	local placeholder = add_text( menu, properties, "PreviewPlaceholder", 1358, 1762, 446, 502, 22, "MAP PREVIEW UNAVAILABLE" )
	placeholder:setHorizontalAlignment( LUI.HorizontalAlignment.Center )

	if properties.headquarters then
		add_image( menu, "MapCaptionBackground", 1346, 1774, 557, 597, 0x000000, 0.7 )
		add_text( menu, properties, "DetailMap", 1358, 1762, 560, 592, 26, "", true ):setupAutoScaleText()
	else
		add_image( menu, "MapCaptionBackground", 1346, 1774, 535, 597, 0x000000, 0.7 )
		add_text( menu, properties, "DetailMap", 1358, 1762, 538, 568, 26, "", true ):setupAutoScaleText()
		add_text( menu, properties, "DetailMode", 1358, 1762, 568, 592, 20 ):setupAutoScaleText()
	end

	add_text( menu, properties, "DetailStatus", 1346, 1644, 612, 644, 22 ):setupAutoScaleText()
	local ping = add_text( menu, properties, "DetailPing", 1644, 1774, 612, 644, 22, "", true )
	ping:setHorizontalAlignment( LUI.HorizontalAlignment.Right )
	add_text( menu, properties, "DetailAddress", 1346, 1774, 656, 688, 20 )

	add_button( menu, properties, "JoinButton", 1346, 1774, properties.headquarters and 690 or 754, "JOIN SERVER", "Join the selected server.", browser.join_selected_server )
	add_button( menu, properties, "FavoriteButton", 1346, 1774, properties.headquarters and 754 or 818, "ADD FAVORITE", "Save or remove this server from your favorites.", browser.toggle_favorite )
	add_text( menu, properties, "ActionMessage", 1330, 1790,
		properties.headquarters and 838 or 902, properties.headquarters and 899 or 963, 20 )

	local helper = LUI.MenuBuilder.BuildRegisteredType( "button_helper_bar", properties )
	helper:setAnchorsAndPosition( 0, 0, 1, 0, 0, 0, -92 * _1080p, -42 * _1080p, 0 )
	menu:addElement( helper )
	menu.ButtonHelperBar = helper

	local controls = { menu.SourceButton, menu.FilterButton, menu.SortButton, menu.RefreshListButton }

	for index, control in ipairs( controls ) do
		control:initNavTables()
		control.navigation = {
			left = controls[index - 1] or controls[#controls],
			right = controls[index + 1] or controls[1],
			down = grid, up = menu.FavoriteButton
		}
	end

	grid:initNavTables()
	grid.navigation = { up = menu.SourceButton, right = menu.JoinButton, down = menu.FilterButton }

	menu.JoinButton:initNavTables()
	menu.JoinButton.navigation = { up = menu.RefreshListButton, down = menu.FavoriteButton, left = grid }

	menu.FavoriteButton:initNavTables()
	menu.FavoriteButton.navigation = { up = menu.JoinButton, down = menu.RefreshListButton, left = grid }

	local timer = LUI.UITimer.new( 250, { name = "s2x_server_browser_tick", dispatchChildren = false }, nil, false, false, false, false )
	timer.timerEventTarget = timer
	timer:addEventHandler( "s2x_server_browser_tick", function ()
		browser.tick( menu )
	end )

	menu:addElement( timer )
	menu.PollingTimer = timer

	browser.PostLoadFunc( menu, controller )

	return menu
end

-- Both entry points share the same layout, controller and discovery snapshot.
builders.s2x_hq_browser = function ( menu, properties )
	properties = properties or {}
	properties.headquarters = true
	return builders.s2x_server_browser( menu, properties )
end
