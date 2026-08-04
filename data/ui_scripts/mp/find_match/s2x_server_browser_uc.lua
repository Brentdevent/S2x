local SERVER_COLUMNS = Engine.IsPC() and {
	Host = 2,
	Map = 3,
	Clients = 4,
	Game = 5,
	Ping = 7,
	Status = 10
} or {
	Host = 0,
	Map = 1,
	Clients = 2,
	Game = 3,
	Ping = 4,
	Status = 5
}

local REFRESH_COOLDOWN_MS = 2000
local SERVER_ROW_FOCUS = "serverButton"

local S2X_LABELS = {
	SERVER_BROWSER = "SERVER BROWSER",
	SERVER_BROWSER_HELPER = "Server Browser",
	SERVER_BROWSER_DESCRIPTION = "Browse dedicated S2x servers.",
	SERVER_BROWSER_FIND_MATCH_DESCRIPTION = "Browse available S2x servers.",
	FIND_MATCH_TITLE = "MODE SELECT",
	FIND_MATCH_DESCRIPTION = "Choose how you want to play.",
	REFRESH = "REFRESH",
	REFRESH_DESCRIPTION = "Refresh the server list.",
	NO_SERVERS_FOUND = "No servers found"
}

local function get_browser_state( element )
	if not element then
		return nil
	end

	local scoped_data = LUI.FlowManager.GetScopedData( element )
	if not scoped_data then
		return nil
	end

	return scoped_data.s2xServerBrowser
end

local function get_controller_index( controller, state )
	if type( controller ) == "number" then
		return controller
	end

	if type( controller ) == "table" and type( controller.controller ) == "number" then
		return controller.controller
	end

	return state and state.controllerIndex or nil
end

local function set_row_text( text_widget, value )
	if text_widget then
		text_widget:setText( value or "" )
	end
end

local function set_list_chrome_alpha( menu, server_count )
	local alpha = server_count > 0 and 1 or 0
	if menu.ScrollBar then
		menu.ScrollBar:setAlpha( alpha )
	end
	if menu.GridPositionIndicator then
		menu.GridPositionIndicator:setAlpha( alpha )
	end
	if menu.EmptyState then
		menu.EmptyState:setAlpha( server_count > 0 and 0 or 1 )
	end
end

local function apply_server_count( menu, state, server_count, force_child_count )
	local count_changed = state.serverCount ~= server_count
	state.serverCount = server_count

	if menu.AvailableGames then
		if count_changed or force_child_count then
			menu.AvailableGames:SetNumChildren( server_count )
			if menu.ScrollBar and LUI.UIVerticalScrollbar then
				LUI.UIVerticalScrollbar.linkTo( menu.ScrollBar, menu.AvailableGames )
			end
		else
			menu.AvailableGames:RefreshContent()
		end
	end

	set_list_chrome_alpha( menu, server_count )
	return count_changed
end

local function focus_refresh_button( menu )
	if menu.AvailableGames then
		menu.AvailableGames:processEvent( {
			name = "lose_focus"
		} )
	end
	if menu.RefreshListButton then
		menu.RefreshListButton:processEvent( {
			name = "gain_focus"
		} )
	end
end

local function focus_server_list( menu )
	if menu.AvailableGames then
		menu.AvailableGames:processEvent( {
			name = "gain_focus"
		} )
	end
	if menu.RefreshListButton then
		menu.RefreshListButton:processEvent( {
			name = "lose_focus"
		} )
	end
end

local function join_focused_server( menu, controller )
	local state = get_browser_state( menu )
	local server_list = menu and menu.AvailableGames or nil
	if not state or not server_list then
		return
	end

	if state.lastFocusedElement ~= server_list and
		state.lastFocusedElement ~= SERVER_ROW_FOCUS then
		return
	end
	if CONDITIONS.IsE3HostMachine() then
		return
	end

	local server_index = server_list:GetFocusPosition( LUI.DIRECTION.vertical )
	local server_count = state.serverCount or 0
	if type( server_index ) ~= "number" or server_index < 0 or
		server_index >= server_count then
		return
	end

	if not server_list.GetElementAtPosition then
		return
	end
	local focused_row = server_list:GetElementAtPosition( 0, server_index )
	if not focused_row or not focused_row.populated then
		return
	end

	local controller_index = get_controller_index( controller, state )
	if type( controller_index ) ~= "number" then
		return
	end

	if CONDITIONS.IsE3Build() then
		Engine.SetDvarBool( "871", true )
	end
	CharacterScene.RunCharacterScene( false )
	state.lastFocusedElement = nil
	Lobby.S2xJoinServer( controller_index, server_index )
end

local function record_focus( menu, focused_element )
	local state = get_browser_state( menu )
	if state then
		state.lastFocusedElement = focused_element
	end
end

local function update_server_list( menu, controller )
	local state = get_browser_state( menu )
	if not state then
		return
	end

	local controller_index = get_controller_index( controller, state )
	if type( controller_index ) ~= "number" then
		return
	end

	local previous_count = state.serverCount or 0
	Lobby.S2xUpdateServerDisplayList( controller_index )
	local server_count = Lobby.S2xGetServerCount( controller_index ) or 0
	local count_changed = apply_server_count( menu, state, server_count, false )

	if count_changed and previous_count > 0 and server_count == 0 then
		focus_refresh_button( menu )
	end
end

local function decrement_refresh_button_cooldown( menu )
	local state = get_browser_state( menu )
	if not state or not state.refreshCooldownMs or state.refreshCooldownMs <= 0 then
		return
	end

	local polling_interval = menu.PollingTimer and menu.PollingTimer.interval or nil
	if type( polling_interval ) ~= "number" or polling_interval <= 0 then
		return
	end

	state.refreshCooldownMs = state.refreshCooldownMs - polling_interval
	if state.refreshCooldownMs <= 0 then
		state.refreshCooldownMs = 0
		if menu.RefreshListButton then
			ACTIONS.RefreshIsButtonDisabled( menu.RefreshListButton )
		end
	end
end

local function is_refresh_disabled( menu )
	local state = get_browser_state( menu )
	return state ~= nil and (state.refreshCooldownMs or 0) > 0
end

local function refresh_server_list( menu, controller, focus_after_refresh )
	local state = get_browser_state( menu )
	if not state or is_refresh_disabled( menu ) then
		return
	end

	local controller_index = get_controller_index( controller, state )
	if type( controller_index ) ~= "number" then
		return
	end

	Lobby.S2xRefreshServerList( controller_index, REFRESH_COOLDOWN_MS )
	local server_count = Lobby.S2xGetServerCount( controller_index ) or 0
	apply_server_count( menu, state, server_count, true )
	state.refreshCooldownMs = REFRESH_COOLDOWN_MS

	if menu.AvailableGames then
		menu.AvailableGames:processEvent( {
			name = "lose_focus"
		} )
	end
	if menu.RefreshListButton then
		ACTIONS.RefreshIsButtonDisabled( menu.RefreshListButton )
	end

	if focus_after_refresh then
		if server_count > 0 then
			focus_server_list( menu )
		else
			focus_refresh_button( menu )
		end
	end
end

local function populate_server_row( row, _, server_index )
	local state = get_browser_state( row )
	local server_count = state and state.serverCount or 0
	local controller_index = state and state.controllerIndex or nil
	local valid_index = type( server_index ) == "number" and server_index >= 0 and
		server_index < server_count

	-- Preserve the existing row styling: every populated numeric row used the
	-- alternate sequence because Lua treats both modulo results as truthy.
	local layout_sequence = type( server_index ) == "number" and
		"AlternateLayout" or "RegularLayout"
	ACTIONS.AnimateSequence( row, layout_sequence )

	row.populated = false
	row.m_inputDisabled = true
	row:hide()

	local host_name, status, map_name, clients, game_mode, ping = nil
	if valid_index and type( controller_index ) == "number" then
		host_name = Lobby.S2xGetServerData( controller_index, server_index, SERVER_COLUMNS.Host )
		status = Lobby.S2xGetServerData( controller_index, server_index, SERVER_COLUMNS.Status )
		map_name = Lobby.S2xGetServerData( controller_index, server_index, SERVER_COLUMNS.Map )
		clients = Lobby.S2xGetServerData( controller_index, server_index, SERVER_COLUMNS.Clients )
		game_mode = Lobby.S2xGetServerData( controller_index, server_index, SERVER_COLUMNS.Game )
		ping = Lobby.S2xGetServerData( controller_index, server_index, SERVER_COLUMNS.Ping )

		row.populated = true
		row.m_inputDisabled = false
		row:show()
	end

	set_row_text( row.HostName, host_name )
	set_row_text( row.Status, status )
	set_row_text( row.MapName, map_name )
	set_row_text( row.Players, clients )
	set_row_text( row.Mode, game_mode )
	set_row_text( row.Ping, ping )

	if type( controller_index ) == "number" then
		ACTIONS.BindSelfToButtonHelperBar( row, controller_index, LuaButton.primary )
	end
end

local function can_focus_list_item( element, _, server_index )
	local state = get_browser_state( element )
	return state ~= nil and type( server_index ) == "number" and
		server_index >= 0 and server_index < (state.serverCount or 0)
end

local function can_focus_server_list( element, focus_type )
	if focus_type == FocusType.Gamepad then
		local state = get_browser_state( element )
		if not state or (state.serverCount or 0) <= 0 then
			Engine.PlaySound( CoD.SFX.Error )
			return false
		end
	end

	return LUI.UIElement.canFocus( element, focus_type )
end

local function preload_server_browser( menu, controller )
	PersistentBackground.Set( PersistentBackground.Variants.HubDefault )

	local scoped_data = LUI.FlowManager.GetScopedData( menu )
	if scoped_data then
		scoped_data.s2xServerBrowser = {
			controllerIndex = controller,
			serverCount = 0,
			lastFocusedElement = nil,
			refreshCooldownMs = 0
		}
	end

	Lobby.S2xBuildServerList( controller )
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
	local function cancel_connection_and_leave()
		Lobby.CancelS2xConnection()
		LUI.FlowManager.RequestLeaveMenu( menu )
	end

	if menu.AvailableGames then
		menu.AvailableGames.canFocus = can_focus_server_list
	end

	if menu.ButtonHelperBar then
		local helper_buttons = menu.ButtonHelperBar:BeginSet()
		helper_buttons:AddLeft( LuaButton.primary, "LUA_MENU_SELECT", join_focused_server )
		if not CONDITIONS.IsE3Build() then
			helper_buttons:AddRight( LuaButton.secondary, "LUA_MENU_BACK", cancel_connection_and_leave )
		end
		helper_buttons:Finish()
		menu.ButtonHelperBar.dontCloseMenusOnStartPress = true
	end

	menu:registerEventHandler( "button_secondary", cancel_connection_and_leave )

	if menu.RefreshListButton then
		menu.RefreshListButton.disabledFunc = function ()
			return is_refresh_disabled( menu )
		end
	end

	refresh_server_list( menu, controller, false )
	menu:registerEventHandler( "gain_focus", function ()
		local state = get_browser_state( menu )
		if state and (state.serverCount or 0) > 0 and menu.AvailableGames then
			menu.AvailableGames:processEvent( {
				name = "gain_focus"
			} )
		elseif menu.RefreshListButton then
			menu.RefreshListButton:processEvent( {
				name = "gain_focus"
			} )
		end
	end )
end

return {
	S2X_LABELS = S2X_LABELS,
	PreLoadFunc = preload_server_browser,
	PostLoadFunc = postload_server_browser,
	decrement_refresh_button_cooldown = decrement_refresh_button_cooldown,
	record_focus = record_focus,
	refresh_server_list = refresh_server_list,
	update_server_list = update_server_list,
	can_focus_list_item = can_focus_list_item,
	populate_server_row = populate_server_row,
	setup_for_e3 = setup_for_e3
}
