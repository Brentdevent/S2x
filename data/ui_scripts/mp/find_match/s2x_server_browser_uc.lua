local model = require( "s2x_server_browser_model" )
local headquarters = require( "s2x_headquarters" )
local REFRESH_COOLDOWN_MS = 2000
local SERVER_ROW_FOCUS = "serverButton"

local S2X_LABELS = {
	SERVER_BROWSER = "SERVER BROWSER",
	SERVER_BROWSER_HELPER = "Server Browser",
	SERVER_BROWSER_DESCRIPTION = "Browse dedicated S2x servers.",
	HQ_BROWSER = "HEADQUARTERS",
	HQ_BROWSER_DESCRIPTION = "Join a public HQ or create an unlisted private HQ.",
	SERVER_BROWSER_FIND_MATCH_DESCRIPTION = "Browse available S2x servers.",
	FIND_MATCH_TITLE = "MODE SELECT",
	FIND_MATCH_DESCRIPTION = "Choose how you want to play."
}

local function get_browser_state( element )
	local scoped = element and LUI.FlowManager.GetScopedData( element )

	return scoped and scoped.s2xServerBrowser
end

local function set_text( widget, text )
	if widget then
		widget:setText( text or "", 0 )
	end
end

local function selected_server( state )
	for _, server in ipairs( state.servers ) do
		if server.address == state.selectedAddress then
			return server
		end
	end
end

local function update_details( menu, state )
	local server = selected_server( state )

	set_text( menu.DetailHost, server and server.hostname or "SELECT A SERVER" )
	set_text( menu.DetailMap, server and (state.headquarters and "Headquarters" or server.map) or "" )
	set_text( menu.DetailMode, server and server.gametype or "" )
	set_text( menu.DetailAddress, server and server.address or "" )
	set_text( menu.DetailStatus, server and server.status or "Browse the list to see server details." )
	set_text( menu.DetailPing, server and server.online and (server.ping .. " ms") or "" )
	set_text( menu.FavoriteButton and menu.FavoriteButton.Name,
		server and server.favorite and "REMOVE FAVORITE" or "ADD FAVORITE" )

	local map = server and server.online and server.mapReference or ""

	if state.previewMap ~= map then
		state.previewMap = map
		menu.MapPreview:setAlpha( 0, 0 )
		set_text( menu.PreviewPlaceholder, "MAP PREVIEW UNAVAILABLE" )
		menu.PreviewPlaceholder:setAlpha( 1, 0 )

		if map ~= "" then
			-- Same map lookup and material cache used by the dedicated lobby.
			local image = Lobby.GetMapImage( map )

			if image and image ~= "" then
				menu.MapPreview:setImage( RegisterMaterial( MODIFIERS.CacheIconMaterial( image ) ), 0 )
				menu.MapPreview:setAlpha( 1, 0 )
				menu.PreviewPlaceholder:setAlpha( 0, 0 )
			end
		end
	end

	ACTIONS.RefreshIsButtonDisabled( menu.JoinButton )
	ACTIONS.RefreshIsButtonDisabled( menu.FavoriteButton )
end

local function record_focus( menu, element )
	local state = get_browser_state( menu )

	if state and not state.updating then
		state.lastFocusedElement = element
	end
end

local function select_row( row )
	local state = get_browser_state( row )

	if state and row.server and not state.updating then
		state.selectedAddress = row.server.address
		state.lastFocusedElement = SERVER_ROW_FOCUS
		update_details( state.menu, state )
	end
end

local function join_selected_server( menu )
	local state = get_browser_state( menu )
	local server = state and selected_server( state )

	if not server or not server.online or CONDITIONS.IsE3HostMachine() then
		return
	end

	if Lobby.JoinServerAddress( server.address ) then
		if CONDITIONS.IsE3Build() then
			Engine.SetDvarBool( "871", true )
		end

		-- HQ owns its frontend scene during the native full-HQ transition.
		if not state.headquarters then
			CharacterScene.RunCharacterScene( false )
		end
	end
end

local function join_focused_server( menu )
	local state = get_browser_state( menu )

	if state and (state.lastFocusedElement == SERVER_ROW_FOCUS or
		state.lastFocusedElement == menu.AvailableGames) then
		join_selected_server( menu )
	end
end

local function update_server_list( menu )
	local state = get_browser_state( menu )

	if not state then
		return
	end

	local snapshot = Lobby.GetServerSnapshot()
	local servers = model.visibleServers( snapshot.servers, state.favoritesOnly, state.filter, state.sort, state.headquarters )
	local index = model.selectionIndex( servers, state.selectedAddress )
	local was_list_focused = state.lastFocusedElement == SERVER_ROW_FOCUS or
		state.lastFocusedElement == menu.AvailableGames
	local old_count = #state.servers
	local old_index = model.selectionIndex( state.servers, state.selectedAddress )
	local rows_changed = not model.sameServers( state.servers, servers )

	state.updating = true
	state.servers = servers
	state.selectedAddress = index and servers[index].address or nil

	if old_count ~= #servers then
		menu.AvailableGames:SetNumChildren( #servers )
	end

	if rows_changed then
		-- SetNumChildren can keep existing cell content. Rebind changed rows before
		-- restoring selection, but leave the native hover/focus state alone on idle ticks.
		menu.AvailableGames:RefreshContent()
	end

	-- Reapplying focus to the same row retriggers its hover sound in the native grid.
	if index and (old_count ~= #servers or old_index ~= index) then
		local position = { x = 0, y = index - 1 }

		if was_list_focused then
			menu.AvailableGames:SetFocusedPosition( position, true, true )
		else
			menu.AvailableGames:SetPosition( position, true )
		end
	end

	state.updating = false

	if old_count > 0 and #servers == 0 and was_list_focused then
		menu.AvailableGames:processEvent( { name = "lose_focus" } )
		menu.RefreshListButton:processEvent( { name = "gain_focus" } )
	end

	menu.GridPositionIndicator:setAlpha( #servers > 0 and 1 or 0, 0 )
	menu.EmptyState:setAlpha( #servers == 0 and 1 or 0, 0 )
	set_text( menu.EmptyState, snapshot.refreshing and "Searching for servers..." or
		(state.favoritesOnly and "No favorites match this filter.\nAdd a server from the Internet list." or
		"No servers match this filter.\nTry another filter or refresh the list.") )
	set_text( menu.SourceButton.Name, state.favoritesOnly and "SOURCE: FAVORITES" or "SOURCE: INTERNET" )
	set_text( menu.SortButton.Name, "SORT: " .. state.sortLabels[state.sort] )
	set_text( menu.FilterButton.Name, "SHOW: " .. model.filterLabels[state.filter] )

	update_details( menu, state )
end

local function refresh_server_list( menu )
	local state = get_browser_state( menu )

	if not state or state.refreshCooldownMs > 0 then
		return
	end

	Lobby.S2xRefreshServerList( state.controllerIndex, REFRESH_COOLDOWN_MS )
	state.refreshCooldownMs = REFRESH_COOLDOWN_MS
	set_text( menu.ActionMessage, "" )
	ACTIONS.RefreshIsButtonDisabled( menu.RefreshListButton )
	update_server_list( menu )
end

local function tick( menu )
	local state = get_browser_state( menu )

	if not state then
		return
	end

	state.refreshCooldownMs = math.max( 0, state.refreshCooldownMs - 250 )
	ACTIONS.RefreshIsButtonDisabled( menu.RefreshListButton )
	update_server_list( menu )
end

local function change_option( menu, option )
	local state = get_browser_state( menu )

	if option == "source" then
		state.favoritesOnly = not state.favoritesOnly
	elseif option == "sort" then
		state.sort = state.sort % #state.sortLabels + 1
	elseif option == "filter" then
		state.filter = state.filter % #model.filterLabels + 1
	end

	set_text( menu.ActionMessage, "" )
	update_server_list( menu )
end

local function toggle_favorite( menu )
	local state = get_browser_state( menu )
	local server = state and selected_server( state )

	if not server then
		return
	end

	local was_favorite = server.favorite

	if Lobby.ToggleFavorite( server.address ) then
		set_text( menu.ActionMessage, was_favorite and "Favorite removed." or "Server saved to favorites." )
		update_server_list( menu )
	else
		set_text( menu.ActionMessage, "Could not save favorites. Check disk access or the 128-server limit." )
	end
end

local function populate_server_row( row, _, server_index )
	local state = get_browser_state( row )
	local server = state and state.servers[server_index + 1]

	row.server = server
	row.populated = server ~= nil
	row.m_inputDisabled = not row.populated

	if not server then
		row:hide()
		return
	end

	row:show()
	row.Background:setAlpha( server_index % 2 == 0 and 0.65 or 0.4, 0 )
	set_text( row.Favorite, server.favorite and "+" or "" )
	set_text( row.HostName, server.hostname )
	set_text( row.MapName, server.online and server.map or server.status )
	set_text( row.Mode, server.gametype )
	set_text( row.Players, server.online and string.format( "%d/%d", server.clients - server.bots, server.maxClients ) or "--" )
	set_text( row.Bots, server.online and tostring( server.bots ) or "--" )
	set_text( row.Ping, server.online and tostring( server.ping ) or "--" )

	ACTIONS.BindSelfToButtonHelperBar( row, state.controllerIndex, LuaButton.primary )
end

local function can_focus_list_item( element, _, index )
	local state = get_browser_state( element )

	return state ~= nil and type( index ) == "number" and index >= 0 and index < #state.servers
end

local function create_private_hq( menu, controller, helper_bar )
	local state = get_browser_state( menu )

	if state and state.headquarters then
		headquarters.create_private( menu, controller or state.controllerIndex, helper_bar )
	end
end

local function preload_server_browser( menu, controller, properties )
	PersistentBackground.Set( PersistentBackground.Variants.HubDefault )

	local scoped = LUI.FlowManager.GetScopedData( menu )
	local is_hq = properties and properties.headquarters == true or false
	scoped.s2xServerBrowser = {
		menu = menu, controllerIndex = controller, servers = {},
		headquarters = is_hq, sortLabels = is_hq and model.hqSortLabels or model.sortLabels,
		favoritesOnly = false, filter = 1, sort = 1, refreshCooldownMs = 0
	}

	menu.isSignInMenu = true
end

local function setup_for_e3( controller )
	local is_client = CONDITIONS.IsE3ClientMachine() and true or false
	local is_host = CONDITIONS.IsE3HostMachine() and true or false
	assert( is_client ~= is_host,
		"You must be EITHER client OR host, and you MUST be one!" )

	if Engine.UsingStreamingInstall() then
		Engine.ForceUpdateArenas()
	end

	Engine.SetSystemLink( true )
	Engine.SetOnlineGame( false )
	AAR.ClearAAR()
	Engine.SetGameIsPrivateMatch( true )
	Engine.SetDvarBool( "3635", false )
	Engine.Exec( MPConfig.default_systemlink, controller )
	Engine.Exec( "xstartlocalprivateparty" )
	Engine.CacheUserDataForController( controller )
	Cac.SetSelectedControllerIndex( controller )
	Character_Scene.SetMode(
		Character_Scene.Actors.Avatar,
		Character_Scene.Views.CaC_Character,
		controller
	)
end

local function postload_server_browser( menu, controller )
	local state = get_browser_state( menu )

	local function leave()
		Lobby.CancelS2xConnection()
		LUI.FlowManager.RequestLeaveMenu( menu )
	end

	menu.AvailableGames.canFocus = function ( element, focus_type )
		if #state.servers == 0 then
			return false
		end

		return LUI.UIElement.canFocus( element, focus_type )
	end

	menu.RefreshListButton.disabledFunc = function ()
		return state.refreshCooldownMs > 0
	end

	menu.JoinButton.disabledFunc = function ()
		local server = selected_server( state )

		return not server or not server.online
	end

	menu.FavoriteButton.disabledFunc = function ()
		return selected_server( state ) == nil
	end

	local helper = menu.ButtonHelperBar:BeginSet()
	helper:AddLeft( LuaButton.primary, "LUA_MENU_SELECT", join_focused_server )

	if state.headquarters then
		helper:AddLeft( LuaButton.alt1, "Create Private HQ", create_private_hq )
	end

	if not CONDITIONS.IsE3Build() then
		helper:AddRight( LuaButton.secondary, "LUA_MENU_BACK", leave )
	end

	helper:Finish()
	menu.ButtonHelperBar.dontCloseMenusOnStartPress = true
	menu:registerEventHandler( "button_secondary", leave )

	menu:registerEventHandler( "gain_focus", function ()
		if #state.servers > 0 then
			menu.AvailableGames:processEvent( { name = "gain_focus" } )
		else
			menu.SourceButton:processEvent( { name = "gain_focus" } )
		end
	end )

	refresh_server_list( menu )
end

return {
	S2X_LABELS = S2X_LABELS,
	PreLoadFunc = preload_server_browser,
	PostLoadFunc = postload_server_browser,
	setup_for_e3 = setup_for_e3,
	record_focus = record_focus,
	select_row = select_row,
	join_selected_server = join_selected_server,
	create_private_hq = create_private_hq,
	refresh_server_list = refresh_server_list,
	update_server_list = update_server_list,
	change_option = change_option,
	toggle_favorite = toggle_favorite,
	tick = tick,
	populate_server_row = populate_server_row,
	can_focus_list_item = can_focus_list_item
}
