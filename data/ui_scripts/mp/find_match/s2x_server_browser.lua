print("[S2x] Executing patched s2x_server_browser")
local f0_local0 = require( "s2x_server_browser_uc" )
local f0_local1, f0_local2, f0_local3, f0_local4, f0_local5, f0_local6, f0_local7, f0_local8, f0_local9, f0_local10, f0_local11, f0_local12, f0_local13, f0_local14 = nil
if f0_local0 ~= nil and type( f0_local0 ) == "table" then
	f0_local1 = f0_local0.PreLoadFunc
	f0_local2 = f0_local0.PostLoadFunc
	f0_local3 = f0_local0.PushFunc
	f0_local4 = f0_local0.PushOverFunc
	f0_local5 = f0_local0.ResumeFunc
	f0_local6 = f0_local0.PopFunc
	f0_local7 = f0_local0.ACTION_CreateMatch
	f0_local8 = f0_local0.ACTION_DecrementRefreshButtonCooldown
	f0_local9 = f0_local0.ACTION_RecordCurrentFocus
	f0_local10 = f0_local0.ACTION_RefreshGamesList
	f0_local11 = f0_local0.ACTION_SetUpForE3
	f0_local12 = f0_local0.ACTION_UpdateGamesList
	f0_local13 = f0_local0.FUNCTOR_CanFocusListItem
	f0_local14 = f0_local0.FUNCTOR_RefreshGamesListChild
end
local s2x_menu_builders = nil
if LUI and LUI.MenuBuilder then
	s2x_menu_builders = LUI.MenuBuilder.m_types_build
end
if s2x_menu_builders == nil then
	s2x_menu_builders = m_types_build
end
assert( type( s2x_menu_builders ) == "table", "[S2x] Could not find MenuBuilder build table" )

s2x_menu_builders["s2x_server_browser"] = function ( menu, controller )
	local self = LUI.UIGenericNavigator.new( {
		left = 0 * _1080p,
		right = 0 * _1080p,
		top = 0 * _1080p,
		bottom = 0 * _1080p,
		leftAnchor = true,
		rightAnchor = true,
		topAnchor = true,
		bottomAnchor = true
	} )
	self.id = "s2x_server_browser"
	local f1_local1 = controller or {}
	local f1_local2 = f1_local1.controllerIndex
	if not f1_local2 then
		if Engine.InFrontend() then
			local f1_local3 = LUI.FlowManager.GetScopedData( self )
			assert( f1_local3 )
			f1_local2 = f1_local3.exclusiveControllerIndex
		else
			f1_local2 = self:getRootController()
		end
	end
	if f0_local1 then
		f0_local1( self, f1_local2, f1_local1 )
	end
	if CONDITIONS.IsE3Build() then
		f0_local11( f1_local2 )
	end
	self:playSound( "menu_open" )
	local f1_local3 = self
	local ScrollBar = nil
	
	ScrollBar = LUI.UIVerticalScrollbar.new( nil, {
		controllerIndex = f1_local2,
		fontIconSet = f1_local1.fontIconSet,
		scrollbarWidth = 3
	} )
	ScrollBar.id = "ScrollBar"
	self:addElement( ScrollBar )
	self.ScrollBar = ScrollBar
	
	ScrollBar:setAnchors( 0, 1, 0, 1, 0 )
	ScrollBar:setBarAlpha( 0,25 )
	ScrollBar:setBarColor( 0xFFFFFF, 0 )
	ScrollBar:setBottom( _1080p * 843,1, 0 )
	ScrollBar:setLeft( _1080p * 1810, 0 )
	ScrollBar:setRight( _1080p * 1840, 0 )
	ScrollBar:setTop( _1080p * 260, 0 )
	ScrollBar:setTrackAlpha( 0,05 )
	ScrollBar:setTrackColor( 0xFFFFFF, 0 )
	local GridPositionIndicator = nil
	
	GridPositionIndicator = LUI.UIGridPositionIndicator.new( {
		controllerIndex = f1_local2,
		fontIconSet = f1_local1.fontIconSet,
		hasCarots = true
	} )
	GridPositionIndicator.id = "GridPositionIndicator"
	self:addElement( GridPositionIndicator )
	self.GridPositionIndicator = GridPositionIndicator
	
	GridPositionIndicator:setAnchors( 0, 1, 0, 1, 0 )
	GridPositionIndicator:setBottom( _1080p * 873,72, 0 )
	GridPositionIndicator:setFont( FONTS.BodyBoldFont.Font )
	GridPositionIndicator:setFontSize( 20, 0 )
	GridPositionIndicator:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
	GridPositionIndicator:setLeft( _1080p * 100, 0 )
	GridPositionIndicator:setRGBFromInt( SWATCHES.Button.MenuOffWhite, 0 )
	GridPositionIndicator:setRight( _1080p * 228, 0 )
	GridPositionIndicator:setTop( _1080p * 843,1, 0 )
	GridPositionIndicator:setVerticalAlignment( LUI.VerticalAlignment.Bottom )
	local RefreshListButton = nil
	
	RefreshListButton = LUI.MenuBuilder.BuildRegisteredType( "GenericButton", {
		controllerIndex = f1_local2,
		fontIconSet = f1_local1.fontIconSet,
		buttonText = "REFRESH",
		desc_text = "Refresh the server list.",
		useSmallBorder = true
	} )
	RefreshListButton.id = "RefreshListButton"
	self:addElement( RefreshListButton )
	self.RefreshListButton = RefreshListButton
	
	RefreshListButton:setAnchors( 0, 1, 0, 1, 0 )
	RefreshListButton:setBottom( _1080p * 173, 0 )
	RefreshListButton:setLeft( _1080p * 1570, 0 )
	RefreshListButton:setRight( _1080p * 1790, 0 )
	RefreshListButton:setTop( _1080p * 125, 0 )
	local ButtonHelperBar = nil
	
	ButtonHelperBar = LUI.MenuBuilder.BuildRegisteredType( "button_helper_bar", {
		controllerIndex = f1_local2,
		fontIconSet = f1_local1.fontIconSet
	} )
	ButtonHelperBar.id = "ButtonHelperBar"
	self:addElement( ButtonHelperBar )
	self.ButtonHelperBar = ButtonHelperBar
	
	ButtonHelperBar:setAnchors( 0, 0, 1, 0, 0 )
	ButtonHelperBar:setBottom( _1080p * -42,29, 0 )
	ButtonHelperBar:setLeft( _1080p * 0, 0 )
	ButtonHelperBar:setRight( _1080p * 0, 0 )
	ButtonHelperBar:setTop( _1080p * -92,29, 0 )
	local AvailableGames = nil
	
	AvailableGames = LUI.UIGridIW7.new( nil, {
		controllerIndex = f1_local2,
		fontIconSet = f1_local1.fontIconSet,
		buildChild = function ()
			return LUI.MenuBuilder.BuildRegisteredType( "s2x_server_browser_row", {
				controllerIndex = f1_local2,
				isBuildChild = true,
				fontIconSet = f1_local1.fontIconSet
			} )
		end,
		columnWidth = _1080p * 1690,
		rowHeight = _1080p * 40,
		horizontalAlignment = LUI.Alignment.Left,
		horizontalAnchor = LUI.UIGrid.AnchorType.Origin,
		horizontalScrollType = LUI.ScrollType.AnchoredEdge,
		isPositionFocusable = f0_local13,
		maxVisibleColumns = 1,
		maxVisibleRows = 15,
		refreshChild = f0_local14,
		spacingX = _1080p * 0,
		spacingY = _1080p * 0,
		verticalAlignment = LUI.Alignment.Top,
		verticalAnchor = LUI.UIGrid.AnchorType.Origin,
		verticalScrollType = LUI.ScrollType.AnchoredEdge,
		wrapX = false,
		wrapY = false
	} )
	AvailableGames.id = "AvailableGames"
	self:addElement( AvailableGames )
	self.AvailableGames = AvailableGames
	
	AvailableGames:setAnchors( 0, 1, 0, 1, 0 )
	AvailableGames:setBottom( _1080p * 843,1, 0 )
	AvailableGames:setLeft( _1080p * 100, 0 )
	AvailableGames:SetPositionIndicator( GridPositionIndicator )
	AvailableGames:setRight( _1080p * 1791, 0 )
	AvailableGames:setTop( _1080p * 260, 0 )
	local Header = nil
	
	Header = LUI.MenuBuilder.BuildRegisteredType( "s2x_server_browser_row", {
		controllerIndex = f1_local2,
		fontIconSet = f1_local1.fontIconSet,
		PROP_HasBackground = true,
		disableInteractivity = true
	} )
	Header.id = "Header"
	self:addElement( Header )
	self.Header = Header
	
	Header:setAnchors( 0, 1, 0, 1, 0 )
	Header:setBottom( _1080p * 258, 0 )
	Header:setLeft( _1080p * 100, 0 )
	Header:setRight( _1080p * 1790, 0 )
	Header:setTop( _1080p * 218, 0 )
	if Header.HostName then
		Header.HostName:setText( Engine.Localize( "MENU_HOST_NAME" ), 0 )
	end
	if Header.MapName then
		Header.MapName:setText( Engine.Localize( "MENU_MAP" ), 0 )
	end
	if Header.Mode then
		Header.Mode:setHorizontalAlignment( LUI.HorizontalAlignment.Center )
		Header.Mode:setText( Engine.Localize( "MENU_TYPE1" ), 0 )
	end
	if Header.Players then
		Header.Players:setHorizontalAlignment( LUI.HorizontalAlignment.Center )
		Header.Players:setText( Engine.Localize( "MENU_NUMPLAYERS" ), 0 )
	end
	if Header.Status then
		Header.Status:setText( Engine.Localize( "MENU_STATUS_HEADER" ), 0 )
	end
	if Header.Ping then
		Header.Ping:setHorizontalAlignment( LUI.HorizontalAlignment.Center )
		Header.Ping:setText( Engine.Localize( "PING" ), 0 )
	end
	local MenuDescription = nil
	
	MenuDescription = LUI.UIText.new()
	MenuDescription.id = "MenuDescription"
	self:addElement( MenuDescription )
	self.MenuDescription = MenuDescription
	
	if f1_local1.fontIconSet ~= nil then
		MenuDescription:setFontIconSet( f1_local1.fontIconSet )
	end
	MenuDescription:setAnchors( 0, 1, 0, 1, 0 )
	MenuDescription:setBottom( _1080p * 199, 0 )
	MenuDescription:setFont( FONTS.BodyItalicFont.Font )
	MenuDescription:setFontSize( 20, 0 )
	MenuDescription:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
	MenuDescription:setLeft( _1080p * 100, 0 )
	MenuDescription:setRGBFromInt( SWATCHES.Menus.MenuOffWhite, 0 )
	MenuDescription:setRight( _1080p * 900, 0 )
	MenuDescription:setText( Engine.Localize( "Browse dedicated S2x servers." ), 0 )
	MenuDescription:setTop( _1080p * 164, 0 )
	MenuDescription:setVerticalAlignment( LUI.VerticalAlignment.Bottom )
	local PollingTimer = nil
	
	PollingTimer = LUI.UITimer.new( 250, {
		name = "s2x_server_browser_tick",
		dispatchChildren = false
	}, nil, false, false, false, false )
	PollingTimer.timerEventTarget = PollingTimer
	PollingTimer.id = "PollingTimer"
	self:addElement( PollingTimer )
	self.PollingTimer = PollingTimer
	
	local splitscreen_player_list0 = nil
	
	splitscreen_player_list0 = LUI.MenuBuilder.BuildRegisteredType( "splitscreen_player_list", {
		controllerIndex = f1_local2,
		fontIconSet = f1_local1.fontIconSet,
		disableInteractivity = true
	} )
	splitscreen_player_list0.id = "splitscreen_player_list0"
	self:addElement( splitscreen_player_list0 )
	self.splitscreen_player_list0 = splitscreen_player_list0
	
	splitscreen_player_list0:setAnchors( 0, 1, 0, 1, 0 )
	splitscreen_player_list0:setBottom( _1080p * 816,01, 0 )
	splitscreen_player_list0:setAlpha( 0, 0 )
	splitscreen_player_list0:setLeft( _1080p * 86,67, 0 )
	splitscreen_player_list0:setRight( _1080p * 529, 0 )
	splitscreen_player_list0:setTop( _1080p * 725,01, 0 )
	local MenuTitle = nil
	
	MenuTitle = LUI.MenuBuilder.BuildRegisteredType( "GenericMenuTitle", {
		controllerIndex = f1_local2,
		fontIconSet = f1_local1.fontIconSet
	} )
	MenuTitle.id = "MenuTitle"
	self:addElement( MenuTitle )
	self.MenuTitle = MenuTitle
	
	MenuTitle:setAnchors( 0, 1, 0, 1, 0 )
	MenuTitle:setBottom( _1080p * 173, 0 )
	MenuTitle:setLeft( _1080p * 100, 0 )
	MenuTitle:setRight( _1080p * 1000, 0 )
	MenuTitle:setTop( _1080p * 125, 0 )
	if MenuTitle.Title then
		MenuTitle.Title:setFont( FONTS.BodyBoldFont.Font )
		MenuTitle.Title:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
		MenuTitle.Title:setText( Engine.Localize( "SERVER BROWSER" ), 0 )
	end
	if MenuTitle.zm_title_divider0 then
		MenuTitle.zm_title_divider0:setRight( _1080p * 1727, 0 )
	end
	if RefreshListButton.navigation == nil then
		RefreshListButton:initNavTables()
	end
	RefreshListButton.navigation = {
		left = AvailableGames,
		up = AvailableGames,
		right = AvailableGames,
		down = AvailableGames
	}
	if AvailableGames.navigation == nil then
		AvailableGames:initNavTables()
	end
	AvailableGames.navigation = {
		up = RefreshListButton,
		right = RefreshListButton
	}
	self.RefreshListButton:RegisterAnimationSequences( {
		E3NoCreateMatch = {
			{
				function ()
					return self.RefreshListButton:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 1570, _1080p * 1790, _1080p * 125, _1080p * 173, 0 )
				end
			}
		}
	} )
	self._sequences = {
		E3NoCreateMatch = function ()
			self.RefreshListButton:AnimateSequence( "E3NoCreateMatch" )
		end
	}
	RefreshListButton:addEventHandler( "button_action", function ( f7_arg0, f7_arg1 )
		f0_local10( self, f7_arg1.controller or f1_local2, true )
	end )
	RefreshListButton:addEventHandler( "gain_focus", function ( f8_arg0, f8_arg1 )
		f0_local9( self, f8_arg0 )
	end )
	AvailableGames:addEventHandler( "gain_focus", function ( f9_arg0, f9_arg1 )
		f0_local9( self, f9_arg0 )
	end )
	PollingTimer:addEventHandler( "s2x_server_browser_tick", function ( f10_arg0, f10_arg1 )
		f0_local12( self, f10_arg1.controller or f1_local2 )
		f0_local8( self )
	end )
	if f0_local2 then
		f0_local2( self, f1_local2, f1_local1 )
	end
	if CONDITIONS.IsE3Build() and CONDITIONS.IsE3ClientMachine() then
		ACTIONS.AnimateSequence( self, "E3NoCreateMatch" )
	end
	return self
end
if f0_local3 then
	LUI.FlowManager.RegisterStackPushBehaviour( "s2x_server_browser", f0_local3 )
end
if f0_local4 then
	LUI.FlowManager.RegisterStackPushOverBehaviour( "s2x_server_browser", f0_local4 )
end
if f0_local5 then
	LUI.FlowManager.RegisterStackResumeBehaviour( "s2x_server_browser", f0_local5 )
end
if f0_local6 then
	LUI.FlowManager.RegisterStackPopBehaviour( "s2x_server_browser", f0_local6 )
end
