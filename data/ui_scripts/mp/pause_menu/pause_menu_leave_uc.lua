local function is_private_match_host()
	return game:ismultiplayer() and not Engine.InFrontend() and not Engine.IsZombiesMode() and
		Lobby.IsGameHost() and Lobby.IsCustomMatch and Lobby.IsCustomMatch()
end

local function should_disconnect_host()
	return game:ismultiplayer() and not Engine.InFrontend() and not Engine.IsZombiesMode() and
		Lobby.IsGameHost() and ((Lobby.IsCustomMatch and Lobby.IsCustomMatch()) or CONDITIONS.IsSystemLink())
end

local f0_local0 = function ( f1_arg0 )
	Engine.ExecFirstClient( "xstopprivateparty" )
	Engine.ExecFirstClient( "xpartydisbandafterround" )
	Engine.ExecFirstClient( "hostmigration_start" )
end

local f0_local1 = function ( f2_arg0 )
	return Engine.GetOnlineGame()
end

local f0_local2 = function ( f3_arg0 )
	if f0_local1( f3_arg0 ) then
		Engine.ExecFirstClient( "xstopprivateparty" )
		Engine.ExecFirstClient( "disconnect" )
		Engine.ExecFirstClient( "setgameprivatematch 0" )
		Engine.ExecFirstClient( "set 2291 1" )
		Engine.ExecFirstClient( "xstartprivateparty" )
		Engine.SetGameIsRankedMatch( false )
	else
		Engine.ExecFirstClient( "disconnect" )
	end
end

local f0_local3 = function ( f4_arg0 )
	local f4_local0 = Lobby.IsInPrivateParty()
	if f4_local0 then
		f4_local0 = Lobby.IsPrivatePartyHost()
		if f4_local0 then
			f4_local0 = not Lobby.IsAloneInPrivateParty()
		end
	end
	return f4_local0
end

local f0_local4 = function ( f5_arg0, f5_arg1 )
	if f0_local3( f5_arg0 ) then
		LUI.FlowManager.RequestLeaveMenu( f5_arg0, true )
		LUI.FlowManager.RequestAddMenu( f5_arg0, "popup_pull_party", true, f5_arg1.controller )
	else
		Engine.Exec( "onPlayerQuit" )
		Engine.ExecNow( "throttleMatch" )
		if Engine.GetDvarBool( "1080" ) then
			f0_local0( f5_arg0 )
		else
			f0_local2( f5_arg0 )
		end
		LUI.FlowManager.RequestCloseAllMenus( f5_arg0 )
	end
end

local f0_local5 = function ( f6_arg0, f6_arg1 )
	Engine.SetDvarString( "1504", Engine.GetPartyMapName() )
	Engine.SetDvarString( "3356", GetGameModeName() )
	Engine.SetDvarString( "ui_lastgame_gamemode", GameX.GetGameMode() )

	if should_disconnect_host() then
		Engine.ExecFirstClient( "disconnect" )
		LUI.FlowManager.RequestCloseAllMenus( f6_arg0 )
		return
	end

	if CONDITIONS.IsRankedPlay() then
		LUIRankedPlay.ResetCommitToGameGate( f6_arg1.controller )
	end

	if CONDITIONS.IsPublicMatch( f6_arg0 ) then
		f0_local4( f6_arg0, f6_arg1 )
	elseif Engine.GetDvarBool( "393" ) then
		Engine.ExecFirstClient( "disconnect" )
		Engine.ExecFirstClient( "setgameprivatematch 0" )
		Engine.ExecFirstClient( "set 2291 1" )
		Engine.SetPartyMapName( Engine.GetDvarString( "941" ) )
		Engine.SetPartyGameType( Engine.GetDvarString( "2311" ) )
		MatchRules.SetData( "gametype", Engine.GetDvarString( "2311" ) )
		Engine.SetDvarString( "941", "" )
		Engine.SetDvarString( "2311", "" )
	else
		local f6_local0 = Engine.GetDvarBool( "1080" )
		local f6_local1 = Engine.GetLuiRoot()
		if f6_local1 and f6_local1.hudManager then
			f6_local1.hudManager:handleHubModeStart()
		end
		if f6_local0 then
			Engine.NotifyServer( "end_game", 1 )
		else
			f0_local2( f6_arg0 )
		end
		LUI.FlowManager.RequestCloseAllMenus( f6_arg0 )
	end
end

return {
	CanEndPrivateMatch = is_private_match_host,
	PostLoadFunc = function ( f7_arg0, f7_arg1, f7_arg2 )
		local f7_local0 = f7_arg0.button_helper_bar:BeginSet()
		f7_local0 = f7_local0:AddBackButton()
		f7_local0 = f7_local0:AddLeft( LuaButton.primary, "LUA_MENU_SELECT", nil )
		f7_local0:Finish()

		if GameX.IsSplitscreen() then
			ACTIONS.AnimateSequence( f7_arg0, "Splitscreen" )
		end

		f7_arg0.LeaveMatchButton:registerEventHandler( "button_action", function ( element, event )
			local f8_local0 = {
				titleText = Engine.Localize( "LUA_MENU_LEAVE_BUTTON" ),
				accept_func = f0_local5
			}
			if CONDITIONS.IsScorestreakTraining() then
				f8_local0.titleText = Engine.ToUpperCase( Engine.Localize( "MENU_LEAVE_SCORESTREAK_TRAINING" ) )
				f8_local0.descText = Engine.Localize( "MENU_LEAVE_SCORESTREAK_TRAINING_SUBTITLE" )
			elseif CONDITIONS.IsRankedPlay() then
				f8_local0.descText = Engine.Localize( "RANKED_PLAY_LEAVE_MATCH_DESC" )
			elseif CONDITIONS.InMPRankedMatch() then
				f8_local0.descText = Engine.Localize( "LUA_MENU_LEAVE_GAME_DESC" )
			else
				f8_local0.descText = Engine.Localize( "MENU_LEAVE_GAME_CONFIRMATION" )
			end
			LUI.FlowManager.RequestAddMenu( f7_arg0, "yesno_popmenu", false, event.controller, nil, f8_local0 )
		end )

		if CONDITIONS.IsScorestreakTraining() then
			f7_arg0.LeaveMatchButton.Name:setText( Engine.ToUpperCase( Engine.Localize( "MENU_LEAVE_SCORESTREAK_TRAINING" ), 0 ) )
			f7_arg0.ReturnToMatchButton.Name:setText( Engine.ToUpperCase( Engine.Localize( "MENU_STAY_IN_SCORESTREAK_TRAINING" ), 0 ) )
			f7_arg0.ConfirmationText:setText( Engine.Localize( "MENU_LEAVE_SCORESTREAK_TRAINING_CONFIRMATION_TITLE" ) )
			if f7_arg0.WarningText then
				f7_arg0.WarningText:setText( Engine.Localize( "MENU_LEAVE_SCORESTREAK_TRAINING_SUBTITLE" ) )
			end
		elseif CONDITIONS.IsRankedPlay() and f7_arg0.WarningText then
			f7_arg0.WarningText:setText( Engine.Localize( "RANKED_PLAY_LEAVE_MATCH_DESC" ) )
		end
		if f7_arg0.EndMatchButton then
			f7_arg0.EndMatchButton:registerEventHandler( "button_action", function ( element, event )
				if not is_private_match_host() then return end
				LUI.FlowManager.RequestAddMenu( f7_arg0, "yesno_popmenu", true, event.controller, false, {
					titleText = Engine.Localize( "End Match" ),
					descText = Engine.Localize( "Are you sure you want to end the match for all players?" ),
					accept_func = function ()
						if not is_private_match_host() then return end
						Engine.NotifyServer( "end_game", 1 )
						LUI.FlowManager.RequestCloseAllMenus( f7_arg0, event.controller )
					end
				} )
			end )
		end

		f7_arg0.ReturnToMatchButton:registerEventHandler( "button_action", function ( element, event )
			ACTIONS.LeaveMenu( f7_arg0 )
		end )

		if Engine.IsPC() then
			f7_arg0.ShutdownButton.Name:setText( Engine.Localize( "@PLATFORM_QUIT_DESKTOP_CAPS" ) )
			f7_arg0.ShutdownButton:registerEventHandler( "button_action", function ( element, event )
				LUI.FlowManager.RequestAddMenu( f7_arg0, "quit_popmenu", false, event.controller )
			end )
		end

		if Engine.IsZombiesMode() and f7_arg0.WarningText then
			f7_arg0.WarningText:setText( "" )
		end
	end
	
}
