print("[S2x] Executing patched findgame_menu")
local f0_local0 = require( "findgame_menu_uc" )
local f0_local1, f0_local2, f0_local3, f0_local4, f0_local5, f0_local6, f0_local7, f0_local8, f0_local9, f0_local10, f0_local11, f0_local12, f0_local13, f0_local14, f0_local15 = nil
if f0_local0 ~= nil and type( f0_local0 ) == "table" then
	f0_local1 = f0_local0.PreLoadFunc
	f0_local2 = f0_local0.PostLoadFunc
	f0_local3 = f0_local0.PushFunc
	f0_local4 = f0_local0.PushOverFunc
	f0_local5 = f0_local0.ResumeFunc
	f0_local6 = f0_local0.PopFunc
	f0_local7 = f0_local0.ACTION_LocalPlayerJoined
	f0_local8 = f0_local0.ACTION_RefreshExplicitButtons
	f0_local9 = f0_local0.ACTION_RefreshGridDisplay
	f0_local10 = f0_local0.ACTION_ShiftGrid
	f0_local11 = f0_local0.ACTION_TogglePrelobby
	f0_local12 = f0_local0.CONDITION_IsExplicitButtonsLayout
	f0_local13 = f0_local0.FUNCTOR_GetDefaultFocusGrid
	f0_local14 = f0_local0.FUNCTOR_IsFocusable
	f0_local15 = f0_local0.FUNCTOR_RefreshChild
end
local s2x_menu_builders = nil
if LUI and LUI.MenuBuilder then
	s2x_menu_builders = LUI.MenuBuilder.m_types_build
end
if s2x_menu_builders == nil then
	s2x_menu_builders = m_types_build
end
assert( type( s2x_menu_builders ) == "table", "[S2x] Could not find MenuBuilder build table" )

s2x_menu_builders["findgame_menu"] = function ( menu, controller )
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
	self.id = "findgame_menu"
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
	self:playSound( "menu_open" )
	local f1_local3 = self
	local MenuBackground = nil
	
	MenuBackground = LUI.MenuBuilder.BuildRegisteredType( "GenericMenuBackground", {
		controllerIndex = f1_local2,
		fontIconSet = f1_local1.fontIconSet
	} )
	MenuBackground.id = "MenuBackground"
	self:addElement( MenuBackground )
	self.MenuBackground = MenuBackground
	
	MenuBackground:setAlpha( 0, 0 )
	MenuBackground:setAnchors( 0, 0, 0, 0, 0 )
	MenuBackground:setBottom( _1080p * 0, 0 )
	MenuBackground:setLeft( _1080p * 0, 0 )
	MenuBackground:setRight( _1080p * 0, 0 )
	MenuBackground:setTop( _1080p * 0, 0 )
	local ButtonHelperBar = nil
	
	ButtonHelperBar = LUI.MenuBuilder.BuildRegisteredType( "button_helper_bar", {
		controllerIndex = f1_local2,
		fontIconSet = f1_local1.fontIconSet
	} )
	ButtonHelperBar.id = "ButtonHelperBar"
	self:addElement( ButtonHelperBar )
	self.ButtonHelperBar = ButtonHelperBar
	
	ButtonHelperBar:setAnchors( 0, 0, 1, 0, 0 )
	ButtonHelperBar:setBottom( _1080p * -55, 0 )
	ButtonHelperBar:setLeft( _1080p * 0, 0 )
	ButtonHelperBar:setRight( _1080p * 0, 0 )
	ButtonHelperBar:setTop( _1080p * -105, 0 )
	local Menutitle = nil
	
	Menutitle = LUI.MenuBuilder.BuildRegisteredType( "GenericMenuTitle", {
		controllerIndex = f1_local2,
		fontIconSet = f1_local1.fontIconSet
	} )
	Menutitle.id = "Menutitle"
	self:addElement( Menutitle )
	self.Menutitle = Menutitle
	
	Menutitle:setAnchors( 0, 1, 0, 1, 0 )
	Menutitle:setBottom( _1080p * 173, 0 )
	Menutitle:setLeft( _1080p * 100, 0 )
	Menutitle:setRight( _1080p * 1000, 0 )
	Menutitle:setTop( _1080p * 125, 0 )
	if Menutitle.Title then
		Menutitle.Title:setFont( FONTS.BodyBoldFont.Font )
		Menutitle.Title:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
		Menutitle.Title:setText( Engine.Localize( "MODE SELECT" ), 0 )
	end
	if Menutitle.zm_title_divider0 then
		Menutitle.zm_title_divider0:setRight( _1080p * 1727, 0 )
	end
	local JoinLobbySubText = nil
	
	JoinLobbySubText = LUI.UIText.new()
	JoinLobbySubText.id = "JoinLobbySubText"
	self:addElement( JoinLobbySubText )
	self.JoinLobbySubText = JoinLobbySubText
	
	if f1_local1.fontIconSet ~= nil then
		JoinLobbySubText:setFontIconSet( f1_local1.fontIconSet )
	end
	JoinLobbySubText:setAnchors( 0, 1, 0, 1, 0 )
	JoinLobbySubText:setBottom( _1080p * 185, 0 )
	JoinLobbySubText:setFont( FONTS.BodyFont.Font )
	JoinLobbySubText:setFontSize( 18, 0 )
	JoinLobbySubText:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
	JoinLobbySubText:setLeft( _1080p * 100, 0 )
	JoinLobbySubText:setRGBFromInt( SWATCHES.Button.MenuOffWhite, 0 )
	JoinLobbySubText:setRight( _1080p * 800, 0 )
	JoinLobbySubText:setText( Engine.Localize( "Choose how you want to play." ), 0 )
	JoinLobbySubText:setTop( _1080p * 168,2, 0 )
	JoinLobbySubText:setVerticalAlignment( LUI.VerticalAlignment.Top )
	local ClosedAlphaLabel = nil
	if CONDITIONS.IsPreLaunchDemo() then
		ClosedAlphaLabel = LUI.UIText.new()
		ClosedAlphaLabel.id = "ClosedAlphaLabel"
		self:addElement( ClosedAlphaLabel )
		self.ClosedAlphaLabel = ClosedAlphaLabel
		
		if f1_local1.fontIconSet ~= nil then
			ClosedAlphaLabel:setFontIconSet( f1_local1.fontIconSet )
		end
		ClosedAlphaLabel:setAlpha( 0, 0 )
		ClosedAlphaLabel:setAnchors( 0, 1, 0, 1, 0 )
		ClosedAlphaLabel:setBottom( _1080p * 123, 0 )
		ClosedAlphaLabel:setFont( FONTS.BodyFont.Font )
		ClosedAlphaLabel:setFontSize( 14, 0 )
		ClosedAlphaLabel:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
		ClosedAlphaLabel:setLeft( _1080p * 100, 0 )
		ClosedAlphaLabel:setRGBFromInt( SWATCHES.Button.MenuOffWhite, 0 )
		ClosedAlphaLabel:setRight( _1080p * 620, 0 )
		ClosedAlphaLabel:setText( Engine.Localize( "LUA_MENU_PRIVATE_BETA" ), 0 )
		ClosedAlphaLabel:setTop( _1080p * 103, 0 )
		ClosedAlphaLabel:setVerticalAlignment( LUI.VerticalAlignment.Middle )
	end
	local GameLocked = nil
	
	GameLocked = LUI.UIText.new()
	GameLocked.id = "GameLocked"
	self:addElement( GameLocked )
	self.GameLocked = GameLocked
	
	if f1_local1.fontIconSet ~= nil then
		GameLocked:setFontIconSet( f1_local1.fontIconSet )
	end
	GameLocked:setAlpha( 0, 0 )
	GameLocked:setAnchors( 0, 1, 0, 1, 0 )
	GameLocked:setBottom( _1080p * 941,31, 0 )
	GameLocked:setFont( FONTS.BodyBoldFont.Font )
	GameLocked:setFontSize( 24, 0 )
	GameLocked:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
	GameLocked:setLeft( _1080p * 150, 0 )
	GameLocked:setRGBFromInt( SWATCHES.Menus.MenuOffWhite, 0 )
	GameLocked:setRight( _1080p * 1402, 0 )
	GameLocked:setText( Engine.Localize( "MPUI_DESC_FIND_GAME_LOCKED" ), 0 )
	GameLocked:setTop( _1080p * 901,31, 0 )
	GameLocked:setVerticalAlignment( LUI.VerticalAlignment.Middle )
	local PartyImage = nil
	
	PartyImage = LUI.UIImage.new()
	PartyImage.id = "PartyImage"
	self:addElement( PartyImage )
	self.PartyImage = PartyImage
	
	PartyImage:setAlpha( 0, 0 )
	PartyImage:setAnchors( 0, 1, 0, 1, 0 )
	PartyImage:setBottom( _1080p * 941,31, 0 )
	PartyImage:setImage( RegisterMaterial( "icon_party_leader" ), 0 )
	PartyImage:setLeft( _1080p * 100, 0 )
	PartyImage:setRGBFromInt( SWATCHES.Menus.MenuOffWhite, 0 )
	PartyImage:setRight( _1080p * 140, 0 )
	PartyImage:setTop( _1080p * 901,31, 0 )
	local Player_Count_Status0 = nil
	
	Player_Count_Status0 = LUI.MenuBuilder.BuildRegisteredType( "Player_Count_Status", {
		controllerIndex = f1_local2,
		fontIconSet = f1_local1.fontIconSet
	} )
	Player_Count_Status0.id = "Player_Count_Status0"
	self:addElement( Player_Count_Status0 )
	self.Player_Count_Status0 = Player_Count_Status0
	
	Player_Count_Status0:setAnchors( 1, 0, 0, 1, 0 )
	Player_Count_Status0:setBottom( _1080p * 185, 0 )
	Player_Count_Status0:setLeft( _1080p * -674, 0 )
	Player_Count_Status0:setRight( _1080p * -100, 0 )
	Player_Count_Status0:setTop( _1080p * 125, 0 )
	local SplitscreenLabel = nil
	
	SplitscreenLabel = LUI.MenuBuilder.BuildRegisteredType( "splitscreen_controller_label", {
		controllerIndex = f1_local2,
		fontIconSet = f1_local1.fontIconSet
	} )
	SplitscreenLabel.id = "SplitscreenLabel"
	self:addElement( SplitscreenLabel )
	self.SplitscreenLabel = SplitscreenLabel
	
	SplitscreenLabel:setAnchors( 0, 1, 1, 0, 0 )
	SplitscreenLabel:setBottom( _1080p * -110, 0 )
	SplitscreenLabel:setLeft( _1080p * 100, 0 )
	SplitscreenLabel:setRight( _1080p * 1100, 0 )
	SplitscreenLabel:setTop( _1080p * -130, 0 )
	local Grid = nil
	
	Grid = LUI.UIGridIW7.new( nil, {
		controllerIndex = f1_local2,
		fontIconSet = f1_local1.fontIconSet,
		buildChild = function ()
			return LUI.MenuBuilder.BuildRegisteredType( "FindGameButton", {
				controllerIndex = f1_local2,
				isBuildChild = true,
				fontIconSet = f1_local1.fontIconSet
			} )
		end,
		columnWidth = _1080p * 400,
		rowHeight = _1080p * 324,
		defaultFocus = f0_local13,
		horizontalAlignment = LUI.Alignment.Left,
		horizontalAnchor = LUI.UIGrid.AnchorType.Origin,
		horizontalScrollType = LUI.ScrollType.AnchoredEdge,
		isPositionFocusable = f0_local14,
		maxVisibleColumns = DataSources.inFrontend.MP.FindMatch.numColumns:GetValue( f1_local2 ),
		maxVisibleRows = 2,
		refreshChild = f0_local15,
		spacingX = _1080p * 20,
		spacingY = _1080p * 20,
		verticalAlignment = LUI.Alignment.Top,
		verticalAnchor = LUI.UIGrid.AnchorType.Origin,
		verticalScrollType = LUI.ScrollType.AnchoredEdge,
		wrapX = false,
		wrapY = false
	} )
	Grid.id = "Grid"
	self:addElement( Grid )
	self.Grid = Grid
	
	Grid:setAnchors( 0, 0, 0, 1, 0 )
	Grid:setBottom( _1080p * 891, 0 )
	Grid:setLeft( _1080p * 100, 0 )
	Grid:setRight( _1080p * -100, 0 )
	Grid:setTop( _1080p * 216, 0 )
	local ExplicitRightButton = nil
	if f0_local12( self ) then
		ExplicitRightButton = LUI.MenuBuilder.BuildRegisteredType( "ArrowButton", {
			controllerIndex = f1_local2,
			fontIconSet = f1_local1.fontIconSet
		} )
		ExplicitRightButton.id = "ExplicitRightButton"
		self:addElement( ExplicitRightButton )
		self.ExplicitRightButton = ExplicitRightButton
		
		ExplicitRightButton:setAnchors( 1, 0, 0, 1, 0 )
		ExplicitRightButton:setBottom( _1080p * 892, 0 )
		ExplicitRightButton:setLeft( _1080p * -140, 0 )
		ExplicitRightButton:setRight( _1080p * -108, 0 )
		ExplicitRightButton:setTop( _1080p * 216, 0 )
		if ExplicitRightButton.ArrowIcon then
			ExplicitRightButton.ArrowIcon:setImage( RegisterMaterial( "findgame_arrow_right" ), 0 )
		end
	end
	local ExplicitLeftButton = nil
	if f0_local12( self ) then
		ExplicitLeftButton = LUI.MenuBuilder.BuildRegisteredType( "ArrowButton", {
			controllerIndex = f1_local2,
			fontIconSet = f1_local1.fontIconSet
		} )
		ExplicitLeftButton.id = "ExplicitLeftButton"
		self:addElement( ExplicitLeftButton )
		self.ExplicitLeftButton = ExplicitLeftButton
		
		ExplicitLeftButton:setAnchors( 0, 1, 0, 1, 0 )
		ExplicitLeftButton:setBottom( _1080p * 892, 0 )
		ExplicitLeftButton:setLeft( _1080p * 48, 0 )
		ExplicitLeftButton:setRight( _1080p * 80, 0 )
		ExplicitLeftButton:setTop( _1080p * 216, 0 )
		if ExplicitLeftButton.ArrowIcon then
			ExplicitLeftButton.ArrowIcon:setImage( RegisterMaterial( "findgame_arrow_left" ), 0 )
		end
	end
	local f1_local16 = function ()
		local f3_local0 = {
			name = "animation_completed"
		}
		f0_local11( self )
		return nil
	end
	
	self.Grid:RegisterAnimationSequences( {
		PreLobbyIntro = {
			{
				function ()
					return self.Grid:setAlpha( 0, 0 )
				end,
				function ()
					return self.Grid:setAlpha( 1, 281, LUI.EASING.linear )
				end
			},
			{
				function ()
					return self.Grid:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 100, _1080p * 1846, _1080p * 687,5, _1080p * 1362,5, 0 )
				end,
				function ()
					return self.Grid:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 100, _1080p * 1846, _1080p * 216, _1080p * 891, 281, LUI.EASING.outSine )
				end
			}
		},
		PreLobbyOutro = {
			{
				function ()
					return self.Grid:setAlpha( 1, 0 )
				end,
				function ()
					return self.Grid:setAlpha( 0, 125, LUI.EASING.linear )
				end
			},
			{
				function ()
					return self.Grid:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 100, _1080p * 1846, _1080p * 216, _1080p * 891, 0 )
				end,
				function ()
					return self.Grid:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 100, _1080p * 1846, _1080p * 583,81, _1080p * 1258,81, 125, LUI.EASING.linear )
				end
			}
		}
	} )
	self.JoinLobbySubText:RegisterAnimationSequences( {
		PreLobbyIntro = {
			{
				function ()
					return self.JoinLobbySubText:setAlpha( 0, 0 )
				end,
				function ()
					return self.JoinLobbySubText:setAlpha( 0, 281, LUI.EASING.linear )
				end,
				function ()
					return self.JoinLobbySubText:setAlpha( 1, 157, LUI.EASING.linear )
				end
			},
			{
				function ()
					return self.JoinLobbySubText:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 60,4, _1080p * 762,4, _1080p * 160,06, _1080p * 180,06, 0 )
				end,
				function ()
					return self.JoinLobbySubText:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 49, _1080p * 751, _1080p * 160,06, _1080p * 180,06, 250, LUI.EASING.linear )
				end,
				function ()
					return self.JoinLobbySubText:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 100, _1080p * 802, _1080p * 160,06, _1080p * 180,06, 188, LUI.EASING.outSine )
				end
			}
		},
		PreLobbyOutro = {
			{
				function ()
					return self.JoinLobbySubText:setAlpha( 1, 0 )
				end,
				function ()
					return self.JoinLobbySubText:setAlpha( 0, 125, LUI.EASING.linear )
				end
			},
			{
				function ()
					return self.JoinLobbySubText:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 100, _1080p * 802, _1080p * 160,06, _1080p * 180,06, 0 )
				end,
				function ()
					return self.JoinLobbySubText:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 69,13, _1080p * 771,13, _1080p * 160,06, _1080p * 180,06, 125, LUI.EASING.linear )
				end
			}
		}
	} )
	self.MenuBackground:RegisterAnimationSequences( {
		PreLobbyIntro = {
			{
				function ()
					return self.MenuBackground:setAlpha( 0, 0 )
				end,
				function ()
					return self.MenuBackground:setAlpha( 1, 281, LUI.EASING.linear )
				end
			}
		},
		PreLobbyOutro = {
			{
				function ()
					return self.MenuBackground:setAlpha( 1, 0 )
				end,
				function ()
					return self.MenuBackground:setAlpha( 0, 125, LUI.EASING.linear )
				end,
				function ()
					return self.MenuBackground:setAlpha( 0, 94, LUI.EASING.linear )
				end,
				f1_local16
			}
		}
	} )
	self.Menutitle:RegisterAnimationSequences( {
		PreLobbyIntro = {
			{
				function ()
					return self.Menutitle:setAlpha( 0, 0 )
				end,
				function ()
					return self.Menutitle:setAlpha( 0, 281, LUI.EASING.linear )
				end,
				function ()
					return self.Menutitle:setAlpha( 1, 157, LUI.EASING.linear )
				end
			},
			{
				function ()
					return self.Menutitle:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 60,4, _1080p * 763,4, _1080p * 125, _1080p * 173, 0 )
				end,
				function ()
					return self.Menutitle:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 49, _1080p * 752, _1080p * 125, _1080p * 173, 250, LUI.EASING.linear )
				end,
				function ()
					return self.Menutitle:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 100, _1080p * 803, _1080p * 125, _1080p * 173, 188, LUI.EASING.outSine )
				end
			}
		},
		PreLobbyOutro = {
			{
				function ()
					return self.Menutitle:setAlpha( 1, 0 )
				end,
				function ()
					return self.Menutitle:setAlpha( 0, 125, LUI.EASING.linear )
				end
			},
			{
				function ()
					return self.Menutitle:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 100, _1080p * 803, _1080p * 125, _1080p * 173, 0 )
				end,
				function ()
					return self.Menutitle:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 69,13, _1080p * 772,13, _1080p * 125, _1080p * 173, 125, LUI.EASING.linear )
				end
			}
		}
	} )
	self._sequences = {
		PreLobbyIntro = function ()
			self.Grid:AnimateSequence( "PreLobbyIntro" )
			self.JoinLobbySubText:AnimateSequence( "PreLobbyIntro" )
			self.MenuBackground:AnimateSequence( "PreLobbyIntro" )
			self.Menutitle:AnimateSequence( "PreLobbyIntro" )
		end,
		PreLobbyOutro = function ()
			self.Grid:AnimateSequence( "PreLobbyOutro" )
			self.JoinLobbySubText:AnimateSequence( "PreLobbyOutro" )
			self.MenuBackground:AnimateSequence( "PreLobbyOutro" )
			self.Menutitle:AnimateSequence( "PreLobbyOutro" )
		end
	}
	if ExplicitRightButton then
		ExplicitRightButton:addEventHandler( "button_action", function ( f39_arg0, f39_arg1 )
			f0_local10( self, f39_arg1.controller or f1_local2, 1 )
		end )
	end
	if ExplicitLeftButton then
		ExplicitLeftButton:addEventHandler( "button_action", function ( f40_arg0, f40_arg1 )
			f0_local10( self, f40_arg1.controller or f1_local2, -1 )
		end )
	end
	self:addEventHandler( "local_player_joined", function ( f41_arg0, f41_arg1 )
		f0_local7( self, f41_arg1 )
	end )
	self:SubscribeToModel( DataSources.inFrontend.MP.FindMatch.currentFirstColumn:GetModel( f1_local2 ), function ()
		local f42_local0 = {
			name = "datasource_event"
		}
		local f42_local1 = f42_local0.controller or f1_local2
		f0_local9( self, f42_local1 )
		if f0_local12( self ) then
			f0_local8( self, f42_local1 )
		end
	end )
	if f0_local2 then
		f0_local2( self, f1_local2, f1_local1 )
	end
	return self
end
if f0_local3 then
	LUI.FlowManager.RegisterStackPushBehaviour( "findgame_menu", f0_local3 )
end
if f0_local4 then
	LUI.FlowManager.RegisterStackPushOverBehaviour( "findgame_menu", f0_local4 )
end
if f0_local5 then
	LUI.FlowManager.RegisterStackResumeBehaviour( "findgame_menu", f0_local5 )
end
if f0_local6 then
	LUI.FlowManager.RegisterStackPopBehaviour( "findgame_menu", f0_local6 )
end
