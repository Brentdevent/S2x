print("[S2x] Executing patched s2x_server_browser_uc")
local f0_local0 = Engine.IsPC() and {
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
local f0_local1 = 2000
local f0_local2 = function ( f1_arg0, f1_arg1, f1_arg2 )
	local f1_local0 = LUI.FlowManager.GetScopedData( f1_arg0 )
	if not f1_local0 or not f1_local0.s2xServerBrowser or f1_local0.s2xServerBrowser.lastFocusedElement ~= f1_arg0.AvailableGames and f1_local0.s2xServerBrowser.lastFocusedElement ~= "serverButton" then
		return 
	elseif CONDITIONS.IsE3HostMachine() then
		return 
	end
	local f1_local1 = f1_arg0.AvailableGames:GetFocusPosition( LUI.DIRECTION.vertical )
	local f1_local2 = assert
	local f1_local3
	if type( f1_local1 ) ~= "number" or f1_local1 < 0 or f1_local1 >= f1_local0.s2xServerBrowser.serverCount then
		f1_local3 = false
	else
		f1_local3 = true
	end
	f1_local2( f1_local3 )
	if CONDITIONS.IsE3Build() then
		Engine.SetDvarBool( "871", true )
	end
	CharacterScene.RunCharacterScene( false )
	f1_local0.s2xServerBrowser.lastFocusedElement = nil
	Lobby.JoinServer( f1_arg1, f1_local1 )
end

local f0_local3 = function ( f2_arg0, f2_arg1 )
	local f2_local0 = LUI.FlowManager.GetScopedData( f2_arg0 )
	if f2_local0.s2xServerBrowser.inputLocked then
		return 
	elseif CONDITIONS.IsE3Build() then
		assert( CONDITIONS.IsE3HostMachine() )
		PersistentForeground.BlackFadeInSequence( 0 )
		PersistentForeground.BlackFadeOutSequence( 5000 )
	end
	f2_local0.s2xServerBrowser.inputLocked = true
	f2_local0.s2xServerBrowser.lastFocusedElement = nil
	CharacterScene.RunCharacterScene( false )
	Engine.OnLANCreateGame()
	Engine.SetDvarString( "5510", "" )
	Engine.SetGameIsPrivateMatch( true )
	Engine.SetGameIsRankedMatch( false )
	Engine.ExecNow( "xstartlobby", Engine.GetControllerForLocalClient( LocalClient0 ) )
	f2_arg0:addElement( LUI.UITimer.new( 1, "next_menu_1_frame_delay", nil, true ) )
	f2_arg0:registerEventHandler( "next_menu_1_frame_delay", function ( element, event )
		LUI.FlowManager.RequestAddMenu( f2_arg0, "private_lobby", false, f2_arg1, true, {
			fromFindGameMenu = true
		} )
	end )
end

local f0_local4 = function ( f4_arg0, f4_arg1 )
	local f4_local0 = LUI.FlowManager.GetScopedData( f4_arg0 )
	f4_local0.s2xServerBrowser.lastFocusedElement = f4_arg1
end

local f0_local5 = function ( f5_arg0, f5_arg1 )
	local f5_local0 = LUI.FlowManager.GetScopedData( f5_arg0 )
	Lobby.UpdateServerDisplayList( f5_arg1 )
	local f5_local1 = Lobby.GetServerCount( f5_arg1 )
	if f5_local0.s2xServerBrowser.serverCount ~= f5_local1 then
		f5_local0.s2xServerBrowser.serverCount = f5_local1
		f5_arg0.AvailableGames:SetNumChildren( f5_local1 )
		LUI.UIVerticalScrollbar.linkTo( f5_arg0.ScrollBar, f5_arg0.AvailableGames )
	else
		f5_arg0.AvailableGames:RefreshContent()
	end
	local f5_local2 = f5_arg0.ScrollBar
	local f5_local3 = f5_local2
	f5_local2 = f5_local2.setAlpha
	local f5_local4
	if f5_local1 > 0 then
		f5_local4 = 1
		if not f5_local4 then
		
		else
			f5_local2( f5_local3, f5_local4 )
			f5_local2 = f5_arg0.GridPositionIndicator
			f5_local3 = f5_local2
			f5_local2 = f5_local2.setAlpha
			if f5_local1 > 0 then
				f5_local4 = 1
				if not f5_local4 then
				
				else
					f5_local2( f5_local3, f5_local4 )
				end
			end
			f5_local4 = 0
		end
	end
	f5_local4 = 0
end

local f0_local6 = function ( f6_arg0 )
	local f6_local0 = LUI.FlowManager.GetScopedData( f6_arg0 )
	if f6_local0.s2xServerBrowser.refreshButtonCooldown == 0 then
		return 
	end
	local f6_local1 = f6_arg0.PollingTimer.interval
	local f6_local2 = assert
	local f6_local3
	if type( f6_local1 ) ~= "number" or f6_local1 <= 0 then
		f6_local3 = false
	else
		f6_local3 = true
	end
	f6_local2( f6_local3 )
	f6_local0.s2xServerBrowser.refreshButtonCooldown = f6_local0.s2xServerBrowser.refreshButtonCooldown - f6_local1
	if f6_local0.s2xServerBrowser.refreshButtonCooldown <= 0 then
		f6_local0.s2xServerBrowser.refreshButtonCooldown = 0
		ACTIONS.RefreshIsButtonDisabled( f6_arg0.RefreshListButton )
	end
end

local f0_local7 = function ( f7_arg0 )
	local f7_local0 = LUI.FlowManager.GetScopedData( f7_arg0 )
	return f7_local0.s2xServerBrowser.refreshButtonCooldown > 0
end

local f0_local8 = function ( f8_arg0, f8_arg1, f8_arg2 )
	if f0_local7( f8_arg0.RefreshListButton ) then
		return 
	end
	local f8_local0 = LUI.FlowManager.GetScopedData( f8_arg0 )
	Lobby.RefreshServerList( f8_arg1, f0_local1 )
	f8_local0.s2xServerBrowser.serverCount = Lobby.GetServerCount( f8_arg1 )
	f8_local0.s2xServerBrowser.refreshButtonCooldown = f0_local1
	f8_arg0.AvailableGames:RefreshContent()
	f8_arg0.AvailableGames:processEvent( {
		name = "lose_focus"
	} )
	ACTIONS.RefreshIsButtonDisabled( f8_arg0.RefreshListButton )
	if f8_arg2 then
		if f8_local0.s2xServerBrowser.serverCount > 0 then
			f8_arg0.AvailableGames:processEvent( {
				name = "gain_focus"
			} )
			f8_arg0.RefreshListButton:processEvent( {
				name = "lose_focus"
			} )
		else
			f8_arg0.RefreshListButton:processEvent( {
				name = "gain_focus"
			} )
		end
	end
end

local f0_local9 = function ( f9_arg0, f9_arg1, f9_arg2 )
	local f9_local0 = LUI.FlowManager.GetScopedData( f9_arg0 )
	local f9_local1 = f9_local0.s2xServerBrowser.controllerIndex
	ACTIONS.AnimateSequence( f9_arg0, f9_arg2 % 2 and "AlternateLayout" or "RegularLayout" )
	local f9_local2, f9_local3, f9_local4, f9_local5, f9_local6, f9_local7 = nil
	f9_arg0.populated = false
	f9_arg0.m_inputDisabled = true
	f9_arg0:hide()
	if f9_arg2 < f9_local0.s2xServerBrowser.serverCount then
		f9_local2 = Lobby.GetServerData( f9_local1, f9_arg2, f0_local0.Host )
		f9_local3 = Lobby.GetServerData( f9_local1, f9_arg2, f0_local0.Status )
		f9_local4 = Lobby.GetServerData( f9_local1, f9_arg2, f0_local0.Map )
		f9_local5 = Lobby.GetServerData( f9_local1, f9_arg2, f0_local0.Clients )
		f9_local6 = Lobby.GetServerData( f9_local1, f9_arg2, f0_local0.Game )
		f9_local7 = Lobby.GetServerData( f9_local1, f9_arg2, f0_local0.Ping )
		if f9_local0.s2xServerBrowser.serverCount ~= 0 then
			f9_arg0.populated = true
			f9_arg0.m_inputDisabled = false
			f9_arg0:show()
		end
	end
	f9_arg0.HostName:setText( f9_local2 or "" )
	f9_arg0.Status:setText( f9_local3 or "" )
	f9_arg0.MapName:setText( f9_local4 or "" )
	f9_arg0.Players:setText( f9_local5 or "" )
	f9_arg0.Mode:setText( f9_local6 or "" )
	f9_arg0.Ping:setText( f9_local7 or "" )
	ACTIONS.BindSelfToButtonHelperBar( f9_arg0, f9_local1, LuaButton.primary )
end

local f0_local10 = function ( f10_arg0, f10_arg1, f10_arg2 )
	local f10_local0 = LUI.FlowManager.GetScopedData( f10_arg0 )
	return f10_arg2 < f10_local0.s2xServerBrowser.serverCount
end

local f0_local11 = function ( f11_arg0, f11_arg1 )
	if f11_arg1 == FocusType.Gamepad then
		local f11_local0 = LUI.FlowManager.GetScopedData( f11_arg0 )
		if f11_local0.s2xServerBrowser.serverCount <= 0 then
			Engine.PlaySound( CoD.SFX.Error )
			return false
		end
	end
	return LUI.UIElement.canFocus( f11_arg0, f11_arg1 )
end

return {
	PreLoadFunc = function ( f13_arg0, f13_arg1, f13_arg2 )
		PersistentBackground.Set( PersistentBackground.Variants.HubDefault )
		local f13_local0 = LUI.FlowManager.GetScopedData( f13_arg0 )
		f13_local0.s2xServerBrowser = {
			controllerIndex = f13_arg1,
			serverCount = 0,
			lastFocusedElement = nil,
			refreshButtonCooldown = 0,
			inputLocked = false
		}
		Lobby.BuildServerList( f13_arg1 )
		f13_arg0.isSignInMenu = true
	end
	,
	PostLoadFunc = function ( f14_arg0, f14_arg1, f14_arg2 )
		f14_arg0.AvailableGames.canFocus = f0_local11
		local f14_local0 = f14_arg0.ButtonHelperBar:BeginSet()
		f14_local0 = f14_local0:AddLeft( LuaButton.primary, "LUA_MENU_SELECT", f0_local2 )
		if not CONDITIONS.IsE3Build() then
			f14_local0:AddRight( LuaButton.secondary, "LUA_MENU_BACK", function ( f15_arg0, f15_arg1 )
				LUI.FlowManager.RequestLeaveMenu( f14_arg0 )
			end )
		end
		f14_local0:Finish()
		f14_arg0:registerEventHandler( "button_secondary", function ( f16_arg0, f16_arg1 )
			LUI.FlowManager.RequestLeaveMenu( f14_arg0 )
		end )
		f14_arg0.RefreshListButton.disabledFunc = f0_local7
		f14_arg0.ButtonHelperBar.dontCloseMenusOnStartPress = true
		f0_local8( f14_arg0, f14_arg1, false )
		f14_arg0:registerEventHandler( "gain_focus", function ()
			local f15_local0 = LUI.FlowManager.GetScopedData( f14_arg0 )
			if f15_local0 and f15_local0.s2xServerBrowser and f15_local0.s2xServerBrowser.serverCount > 0 then
				f14_arg0.AvailableGames:processEvent( {
					name = "gain_focus"
				} )
			elseif f14_arg0.RefreshListButton then
				f14_arg0.RefreshListButton:processEvent( {
					name = "gain_focus"
				} )
			end
		end )
	end
	,
	ACTION_CreateMatch = f0_local3,
	ACTION_RecordCurrentFocus = f0_local4,
	ACTION_DecrementRefreshButtonCooldown = f0_local6,
	FUNCTOR_IsRefreshButtonDisabled = f0_local7,
	ACTION_UpdateGamesList = f0_local5,
	ACTION_RefreshGamesList = f0_local8,
	FUNCTOR_RefreshGamesListChild = f0_local9,
	FUNCTOR_CanFocusListItem = f0_local10,
	ACTION_SetUpForE3 = function ( f12_arg0 )
		local f12_local0 = CONDITIONS.IsE3ClientMachine()
		local f12_local1 = CONDITIONS.IsE3HostMachine()
		local f12_local2 = assert
		local f12_local3
		if not f12_local0 or f12_local1 then
			if not f12_local0 then
				f12_local3 = f12_local1
			else
				f12_local3 = false
			end
		else
			f12_local3 = true
		end
		f12_local2( f12_local3, "You must be EITHER client OR host, and you MUST be one!" )
		if Engine.UsingStreamingInstall() then
			Engine.ForceUpdateArenas()
		end
		Engine.SetSystemLink( true )
		Engine.SetOnlineGame( false )
		AAR.ClearAAR()
		Engine.SetGameIsPrivateMatch( true )
		Engine.SetDvarBool( "3635", false )
		Engine.Exec( MPConfig.default_systemlink, f12_arg0 )
		Engine.Exec( "xstartlocalprivateparty" )
		Engine.CacheUserDataForController( f12_arg0 )
		Cac.SetSelectedControllerIndex( f12_arg0 )
		Character_Scene.SetMode( Character_Scene.Actors.Avatar, Character_Scene.Views.CaC_Character, f12_arg0 )
	end
	
}
