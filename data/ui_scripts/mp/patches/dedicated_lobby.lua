local dedicatedParty = require( "dedicated_party" )
local GetDedicatedPartyMaxPlayers = dedicatedParty.GetMaxPlayers
local GetDedicatedPartyMapName = dedicatedParty.GetMapName
local GetDedicatedPartyGameTypeName = dedicatedParty.GetGameTypeName
local dedicatedMapVoteTag = "public_lobbyscreen_mapvote"

local function AddDedicatedNextMap( page, controller, properties )
	if page.S2xDedicatedNextMap then
		if page.S2xRefreshDedicatedNextMap then
			page.S2xRefreshDedicatedNextMap()
		end

		return
	end

	local panel = LUI.UIElement.new( {
		left = 102 * _1080p,
		right = 557 * _1080p,
		top = 625 * _1080p,
		bottom = 975 * _1080p,
		leftAnchor = true,
		rightAnchor = false,
		topAnchor = true,
		bottomAnchor = false
	} )
	panel.id = "S2xDedicatedNextMap"
	page:addElement( panel )
	page.S2xDedicatedNextMap = panel

	local mapImage = LUI.UIImage.new()
	mapImage.id = "MapImage"
	mapImage:setAnchors( 0, 1, 0, 1, 0 )
	mapImage:setBottom( _1080p * 271.72, 0 )
	mapImage:setLeft( _1080p * 0, 0 )
	mapImage:setRight( _1080p * 455, 0 )
	mapImage:setTop( _1080p * 0, 0 )
	panel:addElement( mapImage )
	panel.MapImage = mapImage

	local mapButton = LUI.MenuBuilder.BuildRegisteredType( "GenericButton", {
		controllerIndex = controller,
		fontIconSet = properties and properties.fontIconSet,
		disableInteractivity = true
	} )
	mapButton.id = "MapButton"
	mapButton:setAnchors( 0, 1, 0, 1, 0 )
	mapButton:setBottom( _1080p * 349.5, 0 )
	mapButton:setLeft( _1080p * 0, 0 )
	mapButton:setRight( _1080p * 455, 0 )
	mapButton:setTop( _1080p * 287.5, 0 )
	if mapButton.ButtonBackground then
		mapButton.ButtonBackground:setLeft( _1080p * 0, 0 )
		mapButton.ButtonBackground:setRight( _1080p * 0, 0 )
	end
	if mapButton.Name then
		mapButton.Name:setFontSize( 24, 0 )
		mapButton.Name:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
	end
	ACTIONS.SetInputEnabled( mapButton, false )
	ACTIONS.SetFocusable( mapButton, false )
	panel:addElement( mapButton )
	panel.MapButton = mapButton

	local mapRef = DataSources and DataSources.inFrontend and
		DataSources.inFrontend.MP and DataSources.inFrontend.MP.lobby and
		DataSources.inFrontend.MP.lobby.mapRef or nil

	local function RefreshNextMap()
		local value = GetDedicatedPartyMapName()
		if (not value or value == "") and mapRef then
			value = mapRef:GetValue( controller )
		end

		if not value or value == "" then
			return
		end

		if mapButton.Name then
			mapButton.Name:setText( Game.GetMapDisplayName( value ), 0 )
		end

		local image = Lobby.GetMapImage( value )
		if image and image ~= "" then
			mapImage:setImage(
				RegisterMaterial( MODIFIERS.CacheIconMaterial( image ) ),
				0
			)
		end
	end

	page.S2xRefreshDedicatedNextMap = RefreshNextMap
	if mapRef then
		mapImage:SubscribeToModel( mapRef:GetModel( controller ), RefreshNextMap )
	end
	RefreshNextMap()
end

local function ConfigureDedicatedLobbyPage( page, controller, properties )
	if page.S2xDedicatedLobbyConfigured then
		AddDedicatedNextMap( page, controller, properties )
		return
	end
	page.S2xDedicatedLobbyConfigured = true

	if page.MapVoteCompact then
		page.MapVoteCompact:setAlpha( 0, 0 )
		ACTIONS.SetInputEnabled( page.MapVoteCompact, false )
		ACTIONS.SetFocusable( page.MapVoteCompact, false )
		page.MapVoteCompact.EnableMouseVoting = function ()
		end
	end

	if page.buttonHelperBar then
		page.buttonHelperBar:HideButtonsWithTag( dedicatedMapVoteTag )
		if page.buttonHelperBar:DoesHaveButton( LuaButton.left_trigger ) then
			page.buttonHelperBar:SetButtonDisabled( LuaButton.left_trigger, true )
			page.buttonHelperBar:ForceSetButtonCallback(
				LuaButton.left_trigger,
				function ()
				end
			)
		end

		if page.buttonHelperBar.ShowButtonsWithTag then
			local showButtonsWithTag = page.buttonHelperBar.ShowButtonsWithTag
			page.buttonHelperBar.ShowButtonsWithTag = function ( element, tag, ... )
				if tag == dedicatedMapVoteTag then
					return
				end

				return showButtonsWithTag( element, tag, ... )
			end
		end
	end

	AddDedicatedNextMap( page, controller, properties )
end

local function RefreshDedicatedLobbyPage( page, controller, properties )
	ConfigureDedicatedLobbyPage( page, controller, properties )

	local gametypeName = GetDedicatedPartyGameTypeName()
	if page.gameType and gametypeName and gametypeName ~= "" then
		page.gameType:setText( Engine.Localize( gametypeName ), 0 )
	end
end

function S2xRefreshDedicatedLobbyPresentation()
	if not GetDedicatedPartyMaxPlayers() then
		return
	end

	local root = Engine.GetLuiRoot()
	local menuInfo = root and root.flowManager and
		LUI.FlowManager.GetTopMenuInfo( root.flowManager.menuInfoStack ) or nil
	local page = menuInfo and menuInfo.menu and menuInfo.menu.CurrentMenuPage or nil
	if page and page.S2xPublicLobbyScreen then
		local controller = page.S2xPublicLobbyController or Engine.GetFirstActiveController()
		RefreshDedicatedLobbyPage( page, controller, page.S2xPublicLobbyProperties )
	end
end

local menuBuilder = LUI and LUI.MenuBuilder or nil
local menuBuilders = menuBuilder and menuBuilder.m_types_build or m_types_build
if menuBuilder and menuBuilders and
	not Lobby.S2xDedicatedLobbyBuilderInstalled then
	local stockPublicLobbyBuilder = menuBuilders["public_lobby_lobbyscreen"]
	if stockPublicLobbyBuilder then
		Lobby.S2xDedicatedLobbyBuilderInstalled = true
		menuBuilders["public_lobby_lobbyscreen"] = function ( menu, properties )
			local page = stockPublicLobbyBuilder( menu, properties )
			local controller = properties and properties.controllerIndex or
				Engine.GetFirstActiveController()
			page.S2xPublicLobbyScreen = true
			page.S2xPublicLobbyController = controller
			page.S2xPublicLobbyProperties = properties
			if GetDedicatedPartyMaxPlayers() then
				RefreshDedicatedLobbyPage( page, controller, properties )
			end

			return page
		end
	end
end
