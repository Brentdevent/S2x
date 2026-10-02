if not game:ismultiplayer() then
	return
end

local function is_custom_match_host()
	return not Engine.InFrontend() and not Engine.IsZombiesMode() and
		Lobby.IsGameHost() and Lobby.IsCustomMatch and Lobby.IsCustomMatch() and Lobby.GetCustomMatchProgression()
end

local menu_builders = LUI.MenuBuilder.m_types_build or m_types_build
local stock_pause_menu_leave = menu_builders["pause_menu_leave"]

menu_builders["pause_menu_leave"] = function ( menu, properties )
	local leave_menu = stock_pause_menu_leave( menu, properties )
	if not is_custom_match_host() then
		return leave_menu
	end

	local button = leave_menu.LeaveMatchButton
	button:registerEventHandler( "button_action", function ( element, event )
		if not is_custom_match_host() then return end
		LUI.FlowManager.RequestAddMenu( leave_menu, "yesno_popmenu", false, event.controller, nil, {
			titleText = Engine.Localize( "LUA_MENU_LEAVE_BUTTON" ),
			descText = Engine.Localize( "MENU_LEAVE_GAME_CONFIRMATION" ),
			accept_func = function ()
				if not is_custom_match_host() then return end
				Engine.SetDvarString( "1504", Engine.GetPartyMapName() )
				Engine.SetDvarString( "3356", GetGameModeName() )
				Engine.SetDvarString( "ui_lastgame_gamemode", GameX.GetGameMode() )

				local root = Engine.GetLuiRoot()
				if root and root.hudManager then
					root.hudManager:handleHubModeStart()
				end

				Engine.NotifyServer( "end_game", 1 )
				LUI.FlowManager.RequestCloseAllMenus( leave_menu )
			end
		} )
	end )
	return leave_menu
end
