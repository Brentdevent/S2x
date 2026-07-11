print("[S2x] Executing patched findgame_menu_uc")
local f0_local0 = 100
local f0_local1 = {
	None = 0,
	HomeMenu = 1,
	ExplicitButtons = 2
}
local f0_local2 = 10
local f0_local3 = 12
local f0_local4 = function ( f1_arg0, f1_arg1 )
	if f1_arg0 ~= nil then
		ACTIONS.SetButtonDisabled( f1_arg0, true )
	else
		DebugPrint( "LUA WARNING: findgame_menu_uc - Attempted to modify " .. f1_arg1 .. " but it didn't exist!" )
	end
end

local f0_local5 = function ()
	return Engine.AllSplitscreenPlayersInParty()
end

local f0_local6 = function ( f3_arg0 )
	local f3_local0 = Lobby.IsInPrivateParty()
	if f3_local0 then
		f3_local0 = not Lobby.IsPrivatePartyHost()
	end
	local f3_local1 = f3_arg0.GameLocked
	local f3_local2 = f3_local1
	f3_local1 = f3_local1.setAlpha
	local f3_local3
	if f3_local0 then
		f3_local3 = 1
		if not f3_local3 then
		
		else
			f3_local1( f3_local2, f3_local3 )
			f3_local1 = f3_arg0.PartyImage
			f3_local2 = f3_local1
			f3_local1 = f3_local1.setAlpha
			if f3_local0 then
				f3_local3 = 1
				if not f3_local3 then
				
				else
					f3_local1( f3_local2, f3_local3 )
					return f3_local0
				end
			end
			f3_local3 = 0
		end
	end
	f3_local3 = 0
end

local f0_local7 = function ()
	local f4_local0 = Lobby.IsInPrivateParty()
	if f4_local0 then
		f4_local0 = Lobby.IsPrivatePartyHost()
		if f4_local0 then
			f4_local0 = not Lobby.IsPartyHostWaitingOnMembers()
		end
	end
	return f4_local0
end

local f0_local8 = function ( f5_arg0, f5_arg1 )
	if not f0_local7() or not f0_local5() then
		return 
	else
		AAR.ClearAAR()
		Engine.Exec( MPConfig.default_xboxlive, f5_arg1.controller )
		Engine.SetGameIsPrivateMatch( true )
		Engine.SetGameIsRankedMatch( false )
		Engine.ExecNow( "xstartprivatematch" )
		UpdatePrivateMatchMaxPlayers()
		LUI.FlowManager.RequestAddMenu( f5_arg0, "private_lobby", false, f5_arg1.controller, false, {
			fromFindGameMenu = true
		} )
		Engine.SendJoinedLobbyMsgToParty()
	end
end

local f0_local9 = function ( f6_arg0, f6_arg1, f6_arg2, f6_arg3 )
	if not f0_local5() then
		return 
	end
	local f6_local0 = f6_arg1.controller
	if not CONDITIONS.IsPreLaunchDemo() and not Engine.GetDvarBool( "2467" ) then
		LUI.FlowManager.RequestAddMenu( nil, "menu_tutorial_modal_container", true, f6_local0, false, {
			tutorialId = 28
		} )
		return 
	end
	AAR.ClearAAR()
	local f6_local1 = ""
	if Matchmaking.IsRestrictedToListenServers( 0 ) then
		f6_local1 = Engine.Localize( "@RANKED_PLAY_NO_DS_REGION" )
	elseif Engine.SplitscreenPlayerCount() > 1 or LUIRankedPlay.PartyHasSplitscreen() then
		f6_local1 = Engine.Localize( "@RANKED_PLAY_TOO_MANY_LOCAL_PLAYERS" )
	elseif PartyUtils.GetMyPartySize( f6_local0 ) > 4 then
		f6_local1 = Engine.Localize( RankedPlay.GetPlaylistTooManyPartyPlayersText() )
	elseif PartyUtils.GetMyPartySize( f6_local0 ) ~= 1 and LUIRankedPlay.AnyPartyMemberLockedOut() and not Engine.GetDvarBool( "5936" ) then
		f6_local1 = RankedPlay.GetRankedPlayMemberLockedOutString()
	elseif not LUIRankedPlay.PartyHasCurrentMMR() then
		f6_local1 = Engine.Localize( "@LUA_MENU_RANKEDPLAY_MEMBER_MMR_UNAVAILABLE" )
	end
	if f6_local1 ~= "" then
		LUI.FlowManager.RequestAddMenu( f6_arg0, "notification_modal", true, f6_local0, false, {
			titleText = Engine.Localize( "@MENU_NOTICE" ),
			descText = f6_local1,
			icon = nil,
			modalType = ModalUtils.NotificationModalType.GeneralNotifications,
			accept_func = function ( f7_arg0, f7_arg1 )
				
			end,
			accept_func_text = Engine.Localize( "@MENU_OK" ),
			handle_event = nil,
			event_handler = nil,
			choices = {}
		} )
		return 
	elseif Playlist.GetItemEnabled( f6_arg2, f6_arg3 ) then
		local f6_local2 = ""
		if not Lobby.IsRankedPlaylistLocked( f6_local0 ) and Lobby.IsRankedPlaylistLockedForAnyLocalPlayer() then
			f6_local2 = RankedPlay.GetRankedPlayMemberLockedOutString()
		elseif not Engine.GetDvarBool( "5935" ) then
			local f6_local3 = Lobby.GetPartyMaxRankedPlayMMRDelta()
			local f6_local4 = Lobby.GetPrivatePartyRankedPlayMMRDelta()
			if f6_local3 <= f6_local4 then
				f6_local2 = Engine.Localize( "@LUA_MENU_RANKEDPLAY_LOCKED_PARTY_SKILL", f6_local4, f6_local3 )
			end
		end
		if f6_local2 ~= "" and not Engine.GetDvarBool( "5936" ) then
			LUI.FlowManager.RequestAddMenu( f6_arg0, "ranked_play_locked", true, f6_local0, false, {
				descText = f6_local2
			} )
		elseif RankedPlay.ShouldShowFTE( f6_local0 ) then
			Engine.ExecNow( "exec mp/stats_init_competitive_loadouts.cfg" )
			LUI.FlowManager.RequestAddMenu( nil, "ranked_play_fte_menu", false, f6_local0, false, {
				category = f6_arg2,
				index = f6_arg3
			} )
		elseif RankedPlay.ShouldShowSeasonStartAdjustment( f6_local0 ) then
			LUI.FlowManager.RequestAddMenu( nil, "ranked_play_season_start", false, f6_local0, false, {
				category = f6_arg2,
				index = f6_arg3
			} )
		else
			RankedPlay.StartLobbyAction( f6_arg3, f6_arg2, f6_local0 )
		end
	else
		LUI.FlowManager.RequestAddMenu( f6_arg0, "playlist_locked", true, f6_local0, false )
	end
end

local f0_local10 = function ( f8_arg0, f8_arg1 )
	if not f0_local5() then
		return 
	elseif GameBattles.RefreshSchedule() == 0 then
		GameBattlesUtils.ResetMatch()
	end
	local f8_local0 = GameBattles.GetAccountState()
	if f8_local0 == GameBattlesUtils.ACCOUNT_STATE.NO_ACCOUNT or f8_local0 == GameBattlesUtils.ACCOUNT_STATE.ACCOUNT_ERROR then
		GameBattles.RefreshAccount()
	end
	AAR.ClearAAR()
	Playlist.ResetRankedPlayPlaylist()
	LUI.FlowManager.RequestAddMenu( f8_arg0, "GameBattlesLobby", true, f8_arg1.controller )
end

local f0_local11 = function ( f9_arg0, f9_arg1 )
	Engine.SetDvarBool( "2474", false )
	if Lobby.IsSessionInitialized( CoD.Lobbies.GameLobby ) then
		Engine.Exec( "xpartygo", 0 )
		Engine.SetDvarInt( "901", 1 )
		Engine.SetDvarInt( "4853", 1 )
		LUI.UITimer.Disable( f9_arg1.timer )
	end
end

function LaunchZombiesTraining( f10_arg0, f10_arg1 )
	if PartyUtils.GetMyPartySize( f10_arg1 ) > 1 then
		return 
	end
	Engine.SetDvarBool( "1316", true )
	local f10_local0 = LUI.FlowManager.GetScopedData( "hub_menu" )
	f10_local0.launchingTrainingMap = true
	Engine.SetDvarString( "3111", "" )
	Engine.ExecNow( "xstartprivateparty" )
	Engine.SetPartyGameType( "zombies" )
	Engine.SetPartyMapName( "mp_zombie_training" )
	Engine.SetPartyMaxPlayers( 1 )
	Engine.SetGamePartyMaxPlayers( 4 )
	Engine.SetGameIsPrivateMatch( true )
	Engine.ExecNow( "xstartprivatematchjoindisabled" )
	MatchRules.SetData( "gametype", "zombies" )
	Engine.ExecNow( MPConfig.default_xboxlive, f10_arg1 )
	local f10_local1 = f10_arg0:getParent()
	local f10_local2 = f10_local1 and f10_local1:getParent()
	f10_arg0:addElement( LUI.UITimer.new( 200, "wait_for_training_start" ) )
	f10_arg0:registerEventHandler( "wait_for_training_start", f0_local11 )
	if f10_local2 then
		ACTIONS.SetInputEnabled( f10_local2, false )
	end
end

local f0_local12 = function ( f11_arg0, f11_arg1 )
	local f11_local0 = f11_arg0
	LUI.FlowManager.RequestAddMenu( nil, "notification_modal", false, false, false, {
		titleText = Engine.Localize( "@LUA_MENU_REPLAY_TRAINING_QUESTION" ),
		descText = Engine.Localize( "@LUA_MENU_REPLAY_TRAINING_EXT_QUESTION" ),
		icon = nil,
		modalType = ModalUtils.NotificationModalType.GeneralNotifications,
		cancel_func = function ( f12_arg0, f12_arg1 )
			
		end,
		accept_func = function ( f13_arg0, f13_arg1 )
			LaunchZombiesTraining( f11_local0, f13_arg1.controller )
		end,
		choices = {}
	} )
end

local f0_local13 = function ( f14_arg0 )
	LUI.FlowManager.DispatchEventToCurrentMenu( {
		name = "toggle_prelobby"
	}, f14_arg0 )
end

local f0_local14 = function ( f15_arg0, f15_arg1 )
	LUI.FlowManager.RequestAddMenu( f15_arg0, "confirm_campaign_popup", false, f15_arg1.controller, false, false )
end

local f0_local15 = function ( f16_arg0, f16_arg1 )
	local f16_local0 = Lobby.EnteringLobby()
	local f16_local1 = f16_arg1.controller
	local f16_local2 = ZMUtils.ShatteredPlaylistCategory
	local f16_local3 = ZMUtils.ShatteredPlaylistIndex
	if Playlist.GetItemLockedReason( f16_local2, f16_local3 ) == ZMUtils.PlaylistLockState.UNLOCKED then
		if Playlist.DoAction( f16_local2, f16_local3, false, false ) then
			LUI.FlowManager.RequestAddMenu( nil, "public_lobby", false, f16_local1, false, {
				category = f16_local2,
				index = f16_local3,
				categoryClass = Playlist.GetItemCategoryClass( f16_local2, f16_local3 )
			} )
		end
	else
		local f16_local4 = {}
		if Playlist.DoWeHaveRequiredDLC( f16_local2, f16_local3 ) then
			f16_local4 = {
				titleText = Engine.Localize( "@MENU_NOTICE" ),
				descText = Engine.Localize( "DLC_NOTEVERYONEHASREQUIREDDLC" ),
				icon = nil,
				modalType = ModalUtils.NotificationModalType.GeneralNotifications,
				accept_func = function ( f17_arg0, f17_arg1 )
					
				end,
				accept_func_text = Engine.Localize( "@MENU_OK" ),
				handle_event = nil,
				event_handler = nil,
				choices = {}
			}
		else
			local f16_local5 = "PLATFORM_PLAYLIST_REQUIRES_DLC"
			if Engine.IsSteam() then
				f16_local5 = "PLATFORM_PLAYLIST_REQUIRES_DLC_STEAM"
			end
			f16_local4 = {
				titleText = Engine.Localize( "@MENU_NOTICE" ),
				descText = Engine.Localize( f16_local5 ),
				icon = nil,
				modalType = ModalUtils.NotificationModalType.GeneralNotifications,
				accept_func = function ( f18_arg0, f18_arg1 )
					LUI.FlowManager.RequestAddMenu( menu, "store_menu", true, f16_local1, false, {
						initCharacterScene = true,
						returnToPrevMenu = true,
						linkedItem = "seasonpass"
					} )
				end,
				cancel_func = function ( f19_arg0, f19_arg1 )
					
				end,
				handle_event = nil,
				event_handler = nil,
				choices = {}
			}
		end
		LUI.FlowManager.RequestAddMenu( nil, "notification_modal", true, f16_local1, false, f16_local4 )
	end
end

local f0_local16 = function ( f20_arg0, f20_arg1 )
	LUI.FlowManager.RequestAddMenu( f20_arg0, "confirm_zombies_popup", false, f20_arg1.controller, false, false )
end

local f0_local17 = function ( f21_arg0, f21_arg1 )
	LUI.FlowManager.RequestAddMenu( f21_arg0, "confirm_mp_popup", false, f21_arg1.controller, false, false )
end

local f0_local18 = function ( f22_arg0, f22_arg1 )
	DwDataUtils.UpdateData[DwDataUtils.Vendor.Operation]( f22_arg1.controller, GameChallengeGroup.All )
	if Engine.IsZombiesMode() then
		Cac.SetVirtualLobbyLoadout( f22_arg1.controller, true, nil, nil )
	end
end

local f0_local19 = function ( f23_arg0, f23_arg1 )
	
end

local f0_local20 = function ( f24_arg0 )
	local f24_local0 = LUI.FlowManager.GetScopedData( "hub_menu" )
	if f24_local0 and f24_local0.findgameDefaultPosition then
		if not Engine.IsZombiesMode() then
			return {
				x = math.min( f24_local0.findgameDefaultPosition.x or 0, 3 ),
				y = 0
			}
		end
		return f24_local0.findgameDefaultPosition
	else
		return {
			x = 0,
			y = 0
		}
	end
end

local f0_local21 = {
	{
		gameDvar = "4262",
		globalDvar = "5919",
		playlistFunc = Playlist.DoesPlaylistHaveDoublePlayerRankXP,
		inPartyFunc = Playlist.DoesPlaylistHavePartyDoublePlayerRankXP,
		inPartyDvar = "party_rankXPScale",
		weekendXpScale = "rank",
		type = DoubleXPUtils.XPType.PlayerRank
	},
	{
		gameDvar = "3039",
		globalDvar = "5918",
		playlistFunc = Playlist.DoesPlaylistHaveDoubleDivisionLevelXP,
		inPartyFunc = Playlist.DoesPlaylistHavePartyDoubleDivisionLevelXP,
		inPartyDvar = "party_divisionXPScale",
		weekendXpScale = "division",
		type = DoubleXPUtils.XPType.DivisionLevel
	},
	{
		gameDvar = "858",
		globalDvar = "5917",
		playlistFunc = Playlist.DoesPlaylistHaveDoubleWeaponLevelXP,
		inPartyFunc = Playlist.DoesPlaylistHavePartyDoubleWeaponLevelXP,
		inPartyDvar = "party_weaponXPScale",
		weekendXpScale = "weapon",
		type = DoubleXPUtils.XPType.WeaponLevel
	},
	{
		gameDvar = "239",
		playlistFunc = Playlist.DoesPlaylistHaveDoubleLoot,
		type = DoubleXPUtils.XPType.Loot
	}
}
local f0_local22 = function ( f25_arg0, f25_arg1, f25_arg2 )
	if f25_arg0.playlistFunc and f25_arg0.playlistFunc( f25_arg1 ) then
		return true
	elseif f25_arg0.gameDvar and Engine.GetDvarInt( f25_arg0.gameDvar ) > 1 then
		return true
	elseif f25_arg0.globalDvar and Engine.GetDvarInt( f25_arg0.globalDvar ) > 1 then
		return true
	elseif f25_arg0.weekendXpScale and Engine.GetWeekendXPScale( f25_arg0.weekendXpScale ) > 1 then
		return true
	elseif not f25_arg2 then
		return false
	elseif f25_arg0.inPartyFunc and f25_arg0.inPartyFunc( f25_arg1 ) then
		return true
	elseif f25_arg0.inPartyDvar and Engine.GetDvarInt( f25_arg0.inPartyDvar ) > 1 then
		return true
	else
		return false
	end
end

local f0_local23 = function ( f26_arg0, f26_arg1 )
	local f26_local0 = {}
	local f26_local1 = Playlist.GetItemPlaylistId( f26_arg0, f26_arg1 )
	local f26_local2 = not Lobby.IsAloneInPrivateParty()
	for f26_local3 = 1, #f0_local21, 1 do
		if f0_local22( f0_local21[f26_local3], f26_local1, f26_local2 ) then
			table.insert( f26_local0, f0_local21[f26_local3].type )
		end
	end
	return f26_local0
end

local f0_local24 = {
	"menu_playlist_advert_background_a",
	"menu_playlist_advert_background_b",
	"menu_playlist_advert_background_c",
	"menu_playlist_advert_background_d"
}
local f0_local25 = function ()
	return Lobby.IsInPrivateParty() and not Lobby.IsPrivatePartyHost()
end

local f0_local26 = function ( f28_arg0, f28_arg1 )
	local f28_local0 = CATEGORYCLASS_ADVERTISING
	local f28_local1 = Playlist.GetCategoryCount( f28_local0 )
	local f28_local2 = assert
	local f28_local3
	if f28_local1 < 0 or f28_local1 > #f0_local24 then
		f28_local3 = false
	else
		f28_local3 = true
	end
	f28_local2( f28_local3 )
	assert( f28_local1 % 2 == 0, "The number of advertising buttons must be an even number." )
	f28_local2 = setmetatable( {
		element = f28_arg0
	}, {
		__mode = "v"
	} )
	f28_local3 = table.create( f28_local1, 0 )
	for f28_local4 = 0, f28_local1 - 1, 1 do
		local f28_local7 = f28_local4
		local f28_local8 = f0_local24[f28_local7 + 1]
		table.insert( f28_local3, {
			image = f28_local8,
			blurImage = f28_local8,
			text = Playlist.GetItemName( f28_local0, f28_local7 ),
			desc = Playlist.GetItemDesc( f28_local0, f28_local7 ),
			disabledFunc = function ()
				local f29_local0
				if Playlist.GetItemEnabled( f28_local0, f28_local7 ) then
					f29_local0 = f0_local25()
				else
					f29_local0 = true
				end
				return f29_local0
			end,
			actionFunc = function ( f30_arg0, f30_arg1 )
				if not f0_local5() then
					return 
				elseif f0_local25() then
					return 
				else
					ACTION_SelectGameMode( f28_local2.element, f30_arg1.controller or f28_arg1, f28_local0, f28_local7 )
				end
			end,
			playlistImage = Playlist.GetItemImage( f28_local0, f28_local7 ),
			playlistBonuses = f0_local23( f28_local0, f28_local7 )
		} )
	end
	return f28_local3
end

local f0_local27 = function ( f31_arg0, f31_arg1, f31_arg2, f31_arg3 )
	local f31_local0 = f0_local26( f31_arg0, f31_arg1 )
	local f31_local1 = true
	local f31_local2 = 1
	local f31_local3 = 1
	for f31_local4 = 1, #f31_local0, 1 do
		local f31_local7 = f31_local0[f31_local4]
		if f31_local1 then
			table.insert( f31_arg2, f31_local2, f31_local7 )
			f31_local2 = f31_local2 + 1
		else
			table.insert( f31_arg3, f31_local3, f31_local7 )
			f31_local3 = f31_local3 + 1
		end
		f31_local1 = not f31_local1
	end
end

local f0_local28 = function ( f32_arg0, f32_arg1, f32_arg2 )
	Character_Scene.SetMode( Character_Scene.Actors.Avatar, Character_Scene.Views.NoAvatar, f32_arg1 )
	local f32_local0 = setmetatable( {
		element = f32_arg0
	}, {
		__mode = "v"
	} )
	local f32_local1 = {
		image = "menu_playlist_select_public",
		blurImage = "menu_playlist_select_public_blur",
		text = "LUA_MENU_PUBLIC_MATCH_CAPS",
		desc = "LUA_MENU_PUBLIC_MATCH_BUTTON_DESC",
		actionFunc = function ( f33_arg0, f33_arg1 )
			if not f0_local5() then
				return 
			else
				local f33_local0 = f33_arg1.controller or f32_arg1
				AAR.ClearAAR()
				ACTIONS.OpenMenu( "findgame_publicmatch_menu", false, f33_local0 )
			end
		end,
		disabledFunc = function ()
			return f0_local6( f32_local0.element )
		end,
		showDoubleXPSignposting = true
	}
	local f32_local2 = {
		image = "buttonbg_publicmatch_zm",
		blurImage = "buttonbg_publicmatch_zm",
		text = "LUA_MENU_PUBLIC_MATCH_CAPS",
		desc = "LUA_MENU_ZOMBIES_PUBLIC_MATCH_BUTTON_DESC",
		actionFunc = function ( f35_arg0, f35_arg1 )
			if not f0_local5() then
				return 
			else
				ACTIONS.OpenMenu( "zm_select_map", false, f35_arg1.controller or f32_arg1 )
			end
		end,
		disabledFunc = function ()
			return f0_local6( f32_local0.element )
		end
	}
	local f32_local3 = {
		image = "menu_playlist_select_custom",
		blurImage = "menu_playlist_select_custom_blur",
		text = "LUA_MENU_CUSTOM_MATCH_CAPS",
		desc = "LUA_MENU_CUSTOM_MATCH_BUTTON_DESC",
		actionFunc = f0_local8,
		disabledFunc = function ()
			return not f0_local7()
		end
	}
	local f32_local4 = {
		image = "buttonbg_custommatch_zm",
		blurImage = "buttonbg_custommatch_zm",
		text = "LUA_MENU_CUSTOM_MATCH_CAPS",
		desc = "LUA_MENU_ZOMBIES_CUSTOM_MATCH_BUTTON_DESC",
		actionFunc = f0_local8,
		disabledFunc = function ()
			return not f0_local7()
		end
	}
	local f32_local5 = {
		image = "menu_playlist_select_ranked",
		blurImage = "menu_playlist_select_ranked_blur",
		text = "LUA_MENU_RANKED_MATCH_CAPS",
		desc = "LUA_MENU_RANKED_MATCH_BUTTON_DESC",
		countdownTime = 1,
		actionFunc = function ( f39_arg0, f39_arg1 )
			local f39_local0 = RankedPlay.GetPlaylistIndex()
			if f39_local0 ~= RankedPlay.PlaylistInvalidIndex then
				f0_local9( f39_arg0, f39_arg1, RankedPlay.PlaylistCategory, f39_local0 )
			end
		end,
		initFunc = function ( f40_arg0 )
			if f0_local25() then
				f0_local4( f40_arg0, "rankedMatchButton" )
			else
				local f40_local0 = RankedPlay.GetPlaylistIndex()
				if f40_local0 == RankedPlay.PlaylistInvalidIndex or not Playlist.GetItemEnabled( RankedPlay.PlaylistCategory, f40_local0 ) then
					f0_local4( f40_arg0, "rankedMatchButton" )
				end
			end
		end,
		disabledFunc = function ( f41_arg0 )
			if Engine.AnyLocalClientConnectionActive() then
				f41_arg0.Button.SubText:setText( Engine.Localize( "LUA_MENU_RANKED_MATCH_BUTTON_DESC" ) )
				if f0_local25() then
					return true
				else
					local f41_local0 = RankedPlay.GetPlaylistIndex()
					if f41_local0 == RankedPlay.PlaylistInvalidIndex or not Playlist.GetItemEnabled( RankedPlay.PlaylistCategory, f41_local0 ) then
						f41_arg0.Button.SubText:setText( Engine.Localize( RankedPlay.GetPlaylistTooManyPartyPlayersText() ) )
						return true
					else
						return false
					end
				end
			else
				return true
			end
		end
	}
	local f32_local6 = {
		image = "menu_playlist_select_gb",
		blurImage = "menu_playlist_select_gb_blur",
		text = "GAMEBATTLES_LOBBY_TITLE",
		desc = "GAMEBATTLES_MENU_ENTRY",
		actionFunc = f0_local10
	}
	local f32_local7 = {
		image = "menu_playlist_select_hq",
		blurImage = "menu_playlist_select_hq_blur",
		text = "LUA_MENU_HEADQUARTERS",
		desc = "LUA_MENU_HEADQUARTERS_DESC",
		actionFunc = function ()
			Engine.ExecNow( "xpartyfullhub" )
		end,
		disabledFunc = function ()
			return f0_local25()
		end,
		showBreadcrumb = CONDITIONS.CanCompleteHubActivities( f32_arg1 )
	}
	local f32_local8 = {
		image = "menu_zombies_playlist_select_multiplayer",
		blurImage = "menu_zombies_playlist_select_multiplayer",
		text = "MENU_MULTIPLAYER_CAPS",
		desc = "PLATFORM_PLAY_ONLINE_DESC",
		actionFunc = f0_local17
	}
	local f32_local9 = function ( f44_arg0 )
		f44_arg0:SetButtonDisabled( PartyUtils.GetMyPartySize( f32_arg1 ) > 1 )
	end
	
	local f32_local10 = {
		image = "buttonbg_replayprologue_zm",
		blurImage = "buttonbg_replayprologue_zm",
		text = "LUA_MENU_REPLAY_TRAINING_CAPS",
		desc = "MPUI_DESC_ZOMBIE_TRAINING",
		actionFunc = f0_local12,
		initFunc = function ( f45_arg0 )
			f45_arg0:SubscribeToModel( DataSources.Shared.MP.privateParty.membersCount:GetModel( f32_arg1 ), function ()
				f32_local9( f45_arg0 )
			end )
		end
	}
	local f32_local11 = {
		image = "menu_playlist_select_campaign",
		blurImage = "menu_playlist_select_campaign_blur",
		text = "LUA_MENU_CAMPAIGN_CAPS",
		desc = "LUA_MENU_CAMPAIGN_DESC",
		actionFunc = f0_local14
	}
	local f32_local12 = {
		image = "menu_zombies_playlist_select_campaign",
		blurImage = "menu_zombies_playlist_select_campaign",
		text = "LUA_MENU_CAMPAIGN_CAPS",
		desc = "LUA_MENU_CAMPAIGN_DESC",
		actionFunc = f0_local14
	}
	local f32_local13 = {
		image = "menu_playlist_select_zombie",
		blurImage = "menu_playlist_select_zombie_blur",
		text = "LUA_MENU_ZOMBIES_CAPS",
		desc = "LUA_MENU_ZOMBIES_BUTTON_DESC",
		actionFunc = f0_local16
	}
	local f32_local14 = {
		image = "menu_playlist_select_zm_shattered",
		blurImage = "menu_playlist_select_zm_shattered",
		text = "LUA_MENU_DLC3_SHATTERED",
		desc = "LUA_MENU_DLC3_SHATTERED_PLAYLIST_DESC",
		actionFunc = function ( f47_arg0, f47_arg1 )
			local f47_local0 = false
			for f47_local1 = 0, Playlist.GetCategoryCount( PLAYLIST_CATEGORY ) - 1, 1 do
				if Playlist.GetItemCategoryClass( PLAYLIST_CATEGORY, f47_local1 ) == CATEGORYCLASS_SHATTERED then
					f47_local0 = true
					break
				end
			end
			if f47_local0 then
				if not f0_local5() then
					return 
				end
				LUI.FlowManager.RequestAddMenu( f47_arg0, "zm_select_map", false, f47_arg1.controller or f32_arg1, nil, {
					isShatteredPlaylist = true
				} )
			else
				f0_local15( f47_arg0, f47_arg1 )
			end
		end,
		disabledFunc = function ()
			return f0_local6( f32_local0.element )
		end,
		disabledFunc = function ()
			return f0_local6( f32_local0.element )
		end
	}
	local f32_local15 = {
		image = "menu_playlist_select_campaign",
		blurImage = "menu_playlist_select_campaign_blur",
		text = "",
		desc = "LUA_MENU_ZOMBIES_BUTTON_DESC",
		actionFunc = f0_local8,
		showLimitedTime = true,
		showClock = true,
		bannerText = "XBOXLIVE_LOOKING_TO_PLAY"
	}
	local s2x_server_browser_button = {
		image = "menu_playlist_select_public",
		blurImage = "menu_playlist_select_public_blur",
		text = "SERVER BROWSER",
		desc = "Browse available S2x servers.",
		actionFunc = function ( f48_arg0, f48_arg1 )
			if not f0_local5() then
				return
			else
				local f48_local0 = f48_arg1.controller or f32_arg1
				AAR.ClearAAR()
				ACTIONS.OpenMenu( "s2x_server_browser", false, f48_local0 )
			end
		end,
		disabledFunc = function ()
			return f0_local6( f32_local0.element )
		end
	}
	local f32_local16, f32_local17 = nil
	local f32_local18 = Engine.GetDvarBool( "2467" )
	if Engine.IsZombiesMode() then
		if CONDITIONS.IsDLC3Enabled() then
			if Engine.AnyoneHasSpecificDLCPack( Lobby.dlcReferenceName.dlc3 ) then
				f32_local16 = {
					f32_local14,
					f32_local2,
					f32_local4
				}
			else
				f32_local16 = {
					f32_local2,
					f32_local4,
					f32_local14
				}
			end
			f32_local17 = {
				f32_local10,
				f32_local12,
				f32_local8
			}
		else
			f32_local16 = {
				f32_local2,
				f32_local4,
				f32_local10
			}
			f32_local17 = {
				f32_local12,
				f32_local8
			}
		end
	else
		-- S2x: replace the MP Find Match playlist grid.
		-- Use a single row ordered by the most common S2x play paths first.
		-- Do not call f0_local27 here, because that injects the advertising playlist tiles
		-- such as Domination, Shipment 1944, Infected and Ground War: War.
		f32_local16 = {
			s2x_server_browser_button,
			f32_local3,
			f32_local11,
			f32_local13
		}
		f32_local17 = {}
	end
	local f32_local19 = 4
	local f32_local20 = f0_local1.None
	if f32_local19 < math.max( #f32_local16, #f32_local17 ) then
		if not Engine.IsGamepadEnabled( f32_arg1 ) then
			f32_local20 = f0_local1.ExplicitButtons
		else
			f32_local20 = f0_local1.HomeMenu
		end
	end
	local f32_local21 = nil
	if f32_local20 == f0_local1.HomeMenu then
		if #f32_local16 < 6 and #f32_local17 < 6 then
			f32_local21 = 5
		else
			f32_local21 = 6
		end
	else
		f32_local21 = 4
	end
	local f32_local22 = LUI.FlowManager.GetScopedData( f32_arg0 )
	f32_local22.findGameMenu = {
		layoutStyle = f32_local20,
		topButtons = f32_local16,
		bottomButtons = f32_local17,
		numTotalColumns = f32_local21,
		numColWindows = f32_local19,
		controllerIndex = f32_arg1
	}
	local f32_local23 = math.max( 0, math.max( #f32_local16, #f32_local17 ) - f32_local22.findGameMenu.numColWindows )
	DataSources.inFrontend.MP.FindMatch.numColumns:SetValue( f32_arg1, f32_local22.findGameMenu.numTotalColumns )
	DataSources.inFrontend.MP.FindMatch.currentFirstColumn:SetValue( f32_arg1, 0 )
	DataSources.inFrontend.MP.FindMatch.maxFirstColumn:SetValue( f32_arg1, f32_local23 )
end

local f0_local29 = function ( f50_arg0 )
	return false
end

local f0_local30 = function ( f51_arg0, f51_arg1, f51_arg2 )
	if f51_arg2.prelobbyToggle then
		ACTIONS.AnimateSequence( f51_arg0, "PreLobbyIntro" )
	else
		local f51_local0 = f51_arg0:wait( 250 )
		f51_local0.onComplete = function ()
			f51_arg0.MenuBackground:setAlpha( 1, 281 )
		end
		
	end
	f51_arg0:SubscribeToModel( DataSources.inFrontend.MP.lobby.playerCount:GetModel( f51_arg1 ), function ()
		f51_arg0.Player_Count_Status0.LobbyPlayerCount:setText( Lobby.GetCurrentMemberCount( Lobby.MemberListStates.Prelobby ) .. "/" .. Engine.GetPrivatePartyMaxPlayers(), 0 )
		if PartyUtils.AmIPartyLeader( f51_arg1 ) then
			f51_arg0.Player_Count_Status0.LobbyStatus:setText( Engine.Localize( "LUA_MENU_YOU_ARE_PARTY_LEADER" ), 0 )
		elseif PartyUtils.GetMyPartySize( f51_arg1 ) > 1 then
			f51_arg0.Player_Count_Status0.LobbyStatus:setText( Engine.Localize( "LUA_MENU_WAITING_FOR_PARTY_LEADER" ), 0 )
		else
			f51_arg0.Player_Count_Status0.LobbyStatus:setText( "", 0 )
		end
	end )
	local f51_local1 = f51_arg0.ButtonHelperBar:BeginSet()
	f51_local1:AddLeft( LuaButton.primary, "LUA_MENU_SELECT", nil )
	local f51_local2 = f51_local1:AddGoToSocialTabButton()
	if not CONDITIONS.IsPreLaunchDemo() then
		f51_local1:AddLeft( LuaButton.left_trigger, "Server Browser", function ()
			if not f0_local5() then
				return
			end
			AAR.ClearAAR()
			ACTIONS.OpenMenu( "s2x_server_browser", false, f51_arg1 )
		end )
	end
	if not Engine.IsZombiesMode() then
		if ButtonHelperBarUtils.IsHubButtonValid( f51_arg1, true ) then
			if not Engine.UseAlternateHubFlow() then
				f51_local1:AddBackButton( LUI.ButtonHelperBarBuilder.BackButtonTypes.GoToHub )
			end
			f51_local1:AddMiddle( LuaButton.start, "LUA_MENU_HEADQUARTERS", Hub.Callback_GoToHub )
		elseif CONDITIONS.IsHubKillswitched() and not f51_local2 then
			f51_local1:AddMiddle( LuaButton.start, "LUA_MENU_SOCIAL_LOW", function ()
				LUI.FlowManager.DispatchEventToCurrentMenu( {
					name = "switch_to_nav_tab",
					menuName = "Social_Tab_Menu",
					controller = f51_arg1
				}, f51_arg0 )
			end, 0, not CONDITIONS.IsInHubTutorial( f51_arg1 ) )
		end
	end
	f51_local1:AddSupplyDropButton( f51_arg1 )
	f51_local1:Finish()
	if f51_arg0.Soldierscreen_Stats then
		f51_arg0.Soldierscreen_Stats:UpdatePlayerStats( f51_arg1 )
	end
	f0_local6( f51_arg0 )
	local f51_local3 = LUI.FlowManager.GetScopedData( f51_arg0 )
	if f51_local3 then
		f51_arg0.Grid:SetNumColumns( f51_local3.findGameMenu.numTotalColumns )
		f51_arg0.Grid:SetNumChildren( f51_local3.findGameMenu.numTotalColumns * 2 )
		f51_arg0.Grid:RefreshContent()
		f51_arg0.Grid:makeFocusable()
		f51_arg0.Grid:processEvent( {
			name = "gain_focus"
		} )
		if f51_local3.findGameMenu.layoutStyle == f0_local1.ExplicitButtons then
			f51_arg0.ExplicitLeftButton.canFocus = f0_local29
			f51_arg0.ExplicitRightButton.canFocus = f0_local29
		end
	end
	if Engine.IsZombiesMode() then
		f51_arg0.Menutitle.Title:setFont( FONTS.TitleFont.Font )
		local f51_local4 = f51_arg0:wait( 1 )
		f51_local4.onComplete = function ()
			if PartyUtils.GetMyPartySize( f51_arg1 ) == 1 and ZombiesOnboarding.AttemptFirstTouchPopup( f51_arg1, "Welcome" ) then
				f51_arg0:addEventHandler( "menu_tutorial_completed", function ( f57_arg0, f57_arg1 )
					local f57_local0 = LUI.FlowManager.GetScopedData( "hub_menu" )
					if not f57_local0.launchingTrainingMap then
						local f57_local1 = f57_arg0:wait( 1 )
						f57_local1.onComplete = function ()
							if Engine.AnyoneHasSpecificDLCPack( Lobby.dlcReferenceName.dlc3 ) then
								ZombiesOnboarding.AttemptFirstTouchPopup( f51_arg1, "PublicMatchAlt" )
							else
								ZombiesOnboarding.AttemptFirstTouchPopup( f51_arg1, "PublicMatch" )
							end
						end
						
					end
					if f57_arg0.Grid then
						f57_arg0.Grid:processEvent( {
							name = "gain_focus"
						} )
					end
				end )
			elseif Engine.AnyoneHasSpecificDLCPack( Lobby.dlcReferenceName.dlc3 ) then
				ZombiesOnboarding.AttemptFirstTouchPopup( f51_arg1, "PublicMatchAlt" )
			else
				ZombiesOnboarding.AttemptFirstTouchPopup( f51_arg1, "PublicMatch" )
			end
		end
		
		AAR.ClearAAR()
	else
		f51_arg0:addEventHandler( "menu_tutorial_completed", function ( f59_arg0, f59_arg1 )
			if f59_arg0.Grid then
				f59_arg0.Grid:processEvent( {
					name = "gain_focus"
				} )
			end
		end )
	end
	if GameBattlesUtils.PendingPopup ~= nil then
		GameBattlesUtils.DisplayPendingPopup( f51_arg0, f51_arg1 )
	end
	if not Engine.IsZombiesMode() and CONDITIONS.IsHubAlternateFlow() and not CONDITIONS.ShowHubQuickstartMenu() and not CONDITIONS.IsInHubTutorial( f51_arg1 ) and Engine.SplitscreenPlayerCount() <= 1 then
		if (Engine.GetPlayerData( f51_arg1, CoD.StatsGroup.Ranked, "hubStats", "menuTutorialComplete_hq" ) == false or Engine.GetDvarBool( "hub_forceMenuTutorials" )) and not Engine.GetDvarBool( "hub_preventMenuTutorials" ) then
			LUI.FlowManager.RequestAddMenu( nil, "menu_tutorial_modal_container", true, f51_arg1, false, {
				tutorialId = 53
			} )
			Engine.SetPlayerData( f51_arg1, CoD.StatsGroup.Ranked, "hubStats", "menuTutorialComplete_hq", true )
			ACTIONS.UploadStats()
		end
		local f51_local5 = f51_arg0
		local f51_local4 = f51_arg0.dispatchEventToRoot
		local f51_local6 = {
			name = "update_breadcrumb_state"
		}
		local f51_local7 = {}
		f51_local6.data = CONDITIONS.CanCompleteHubActivities( f51_arg1 )
		f51_local4( f51_local5, f51_local6 )
	end
end

local f0_local31 = function ( f60_arg0, f60_arg1, f60_arg2 )
	if f60_arg0.findGameMenu.layoutStyle == f0_local1.ExplicitButtons then
		f60_arg1 = f60_arg1 + DataSources.inFrontend.MP.FindMatch.currentFirstColumn:GetValue( f60_arg0.findGameMenu.controllerIndex )
	end
	if f60_arg2 == 1 then
		return f60_arg0.findGameMenu.bottomButtons[f60_arg1 + 1]
	else
		return f60_arg0.findGameMenu.topButtons[f60_arg1 + 1]
	end
end

local f0_local32 = 400
local f0_local33 = 20
local f0_local34 = {
	[f0_local1.None] = function ( f61_arg0, f61_arg1 )
		
	end,
	[f0_local1.HomeMenu] = function ( f62_arg0, f62_arg1 )
		f62_arg0.Grid:setLeft( _1080p * (100 - DataSources.inFrontend.MP.FindMatch.currentFirstColumn:GetValue( f62_arg1 ) * (f0_local32 + f0_local33)) )
	end,
	[f0_local1.ExplicitButtons] = function ( f63_arg0, f63_arg1 )
		f63_arg0.Grid:RefreshContent()
		local f63_local0 = LUI.FlowManager.GetScopedData( f63_arg0 )
		local f63_local1 = f63_arg0.Grid:GetFocusPosition( LUI.DIRECTION.vertical )
		local f63_local2 = f63_arg0.Grid:GetFocusPosition( LUI.DIRECTION.horizontal )
		if f63_local1 and f63_local2 and not f0_local31( f63_local0, f63_local2, f63_local1 ) then
			f63_arg0.Grid:SetFocusedPosition( {
				x = f63_local2,
				y = (f63_local1 + 1) % 2
			}, true, false )
		end
	end
}
local f0_local35 = function ( f64_arg0, f64_arg1 )
	local f64_local0 = LUI.FlowManager.GetScopedData( f64_arg0 )
	f0_local34[f64_local0.findGameMenu.layoutStyle]( f64_arg0, f64_arg1 )
end

local f0_local36 = function ( f65_arg0, f65_arg1 )
	local f65_local0 = f65_arg0
	local f65_local1 = f65_arg0.setAlpha
	local f65_local2
	if f65_arg1 then
		f65_local2 = 1
		if not f65_local2 then
		
		else
			f65_local1( f65_local0, f65_local2 )
			f65_arg0:SetButtonDisabled( not f65_arg1 )
		end
	end
	f65_local2 = 0,5
end

local f0_local37 = function ( f66_arg0, f66_arg1 )
	local f66_local0 = DataSources.inFrontend.MP.FindMatch.currentFirstColumn:GetValue( f66_arg1 )
	local f66_local1 = DataSources.inFrontend.MP.FindMatch.maxFirstColumn:GetValue( f66_arg1 )
	f0_local36( f66_arg0.ExplicitLeftButton, f66_local0 > 0 )
	f0_local36( f66_arg0.ExplicitRightButton, f66_local0 < f66_local1 )
end

local f0_local38 = function ( f67_arg0, f67_arg1 )
	DataSources.inFrontend.MP.FindMatch.currentFirstColumn:SetValue( f67_arg0, LUI.clamp( f67_arg1, 0, DataSources.inFrontend.MP.FindMatch.maxFirstColumn:GetValue( f67_arg0 ) ) )
end

local f0_local39 = function ( f68_arg0, f68_arg1, f68_arg2 )
	f0_local38( f68_arg1, DataSources.inFrontend.MP.FindMatch.currentFirstColumn:GetValue( f68_arg1 ) + f68_arg2 )
end

local f0_local40 = function ( f69_arg0 )
	local f69_local0 = f69_arg0:getParent()
	return f69_local0:getParent()
end

local f0_local41 = function ( f70_arg0, f70_arg1 )
	local f70_local0 = LUI.FlowManager.GetScopedData( f70_arg0 )
	local f70_local1 = f70_local0.findGameMenu.controllerIndex
	local f70_local2 = f70_arg0._gridPosition.x
	local f70_local3 = DataSources.inFrontend.MP.FindMatch.currentFirstColumn:GetValue( f70_local1 )
	if f70_local2 < f70_local3 then
		f0_local38( f70_local1, f70_local2 )
		return 
	elseif f70_local3 + f70_local0.findGameMenu.numColWindows <= f70_local2 then
		f0_local38( f70_local1, f70_local2 - f70_local0.findGameMenu.numColWindows + 1 )
		return 
	else
		
	end
end

local f0_local42 = 400
local f0_local43 = 245,5
local f0_local44 = 245,5
local f0_local45 = function ( f71_arg0 )
	if f71_arg0._hasAddedPlaylistIcon == true then
		return 
	else
		local f71_local0 = (f0_local42 - f0_local43) / 2
		local f71_local1 = f71_local0 + f0_local43
		local f71_local2 = 0 + f0_local44
		local self = LUI.UIImage.new()
		self.id = "playlistIcon"
		f71_arg0:insertElement( self, 4 )
		f71_arg0.playlistIcon = self
		self:setAnchorsAndPosition( 0, 1, 0, 1, f71_local0 * _1080p, f71_local1 * _1080p, 0, f71_local2 * _1080p )
		self:setAlpha( 0 )
		f71_arg0._hasAddedPlaylistIcon = true
	end
end

return {
	PreLoadFunc = f0_local28,
	PostLoadFunc = f0_local30,
	ResumeFunc = ResumeFunc,
	FUNCTOR_RefreshChild = function ( f72_arg0, f72_arg1, f72_arg2 )
		local f72_local0 = f0_local40( f72_arg0 )
		if f72_local0 == nil then
			return 
		end
		local f72_local1 = LUI.FlowManager.GetScopedData( f72_local0 )
		local f72_local2 = f0_local31( f72_local1, f72_arg1, f72_arg2 )
		local f72_local3 = f72_arg1 + f72_arg2 * f72_local1.findGameMenu.numTotalColumns + 1
		local f72_local4 = f72_arg0.Button
		f72_arg0:registerEventHandler( "button_over", nil )
		f72_arg0:registerEventHandler( "button_action", nil )
		f72_arg0:registerEventHandler( "button_up_mouse", nil )
		f72_arg0:registerEventHandler( "button_down", nil )
		f72_arg0:registerEventHandler( "button_up", nil )
		f72_arg0:registerEventHandler( "button_right", nil )
		f72_arg0:registerEventHandler( "button_left", nil )
		f72_arg0:registerEventHandler( "button_right_disable", nil )
		f72_arg0:registerEventHandler( "button_left_disable", nil )
		f72_arg0.muteAllSfx = not f72_local2
		f72_local4.muteAllSfx = not f72_local2
		f72_arg0.properties = f72_arg0.properties or {}
		f72_arg0.properties.allowDisabledLeft = true
		f72_arg0.properties.allowDisabledRight = true
		if not f72_local2 then
			f72_arg0:setAlpha( 0 )
			f72_arg0:Disable()
			return 
		end
		f72_arg0:setAlpha( 1 )
		if f72_local2.disabledFunc then
			f72_arg0.disabledFunc = f72_local2.disabledFunc
			f72_arg0:setDisabledRefreshRate( f0_local0 )
		else
			f72_arg0:Enable()
		end
		if f72_local1.findGameMenu.layoutStyle == f0_local1.HomeMenu then
			f72_arg0:registerEventHandler( "button_over", f0_local41 )
			f72_arg0:registerEventHandler( "button_over_disable", f0_local41 )
		elseif f72_local1.findGameMenu.layoutStyle == f0_local1.ExplicitButtons then
			local f72_local5 = DataSources.inFrontend.MP.FindMatch.currentFirstColumn:GetValue( f72_local1.findGameMenu.controllerIndex )
			local f72_local6 = f72_local1.findGameMenu.numColWindows
			if f72_arg1 == 0 and f72_local5 ~= 0 then
				f72_arg0:registerEventHandler( "button_left_disable", function ()
					f0_local39( f72_local0, f72_local1.findGameMenu.controllerIndex, -1 )
				end )
				f72_arg0:registerEventHandler( "button_left", function ()
					f0_local39( f72_local0, f72_local1.findGameMenu.controllerIndex, -1 )
				end )
			elseif f72_arg1 == f72_local6 - 1 then
				f72_arg0:registerEventHandler( "button_right_disable", function ()
					f0_local39( f72_local0, f72_local1.findGameMenu.controllerIndex, 1 )
				end )
				f72_arg0:registerEventHandler( "button_right", function ()
					f0_local39( f72_local0, f72_local1.findGameMenu.controllerIndex, 1 )
				end )
			end
		end
		f72_local4._index = f72_local3
		if f72_local2.image then
			f72_local4.BGImage:setImage( RegisterMaterial( f72_local2.image ), 0 )
		end
		if f72_local2.blurImage then
			f72_local4.BGImageBlurred:setImage( RegisterMaterial( f72_local2.blurImage ), 0 )
		end
		if f72_local2.text then
			f72_local4.TitleLabel:setText( Engine.ToUpperCase( Engine.Localize( f72_local2.text ), 0 ) )
		else
			f72_local4.TitleLabel:setText( "" )
		end
		if f72_local2.desc then
			f72_local4.SubText:setText( Engine.Localize( f72_local2.desc ), 0 )
		else
			f72_local4.SubText:setText( "" )
		end
		if f72_local2.countdownTime then
			RankedPlay.SeasonEndSignPost( f72_arg0.CountdownTime, f72_arg0.CountdownTitle, f72_arg0.CountdownText )
		else
			ACTIONS.Hide( f72_arg0.CountdownTime )
			ACTIONS.Hide( f72_arg0.CountdownTitle )
			ACTIONS.Hide( f72_arg0.CountdownText )
		end
		if f72_local2.actionFunc then
			f72_arg0:registerEventHandler( "button_action", function ( element, event )
				f72_local2.actionFunc( element, event )
				local f77_local0 = LUI.FlowManager.GetScopedData( "hub_menu" )
				f77_local0.findgameDefaultPosition = element._gridPosition
			end )
		end
		f72_arg0.BreadCrumbIcon:setAlpha( f72_local2.showBreadcrumb and 1 or 0 )
		if f72_local2.showLimitedTime then
			ACTIONS.AnimateSequence( f72_arg0, "ShowLimitedTime" )
		else
			ACTIONS.AnimateSequence( f72_arg0, "HideLimitedTime" )
		end
		if f72_local2.showClock then
			ACTIONS.AnimateSequence( f72_arg0, "ShowClock" )
		else
			ACTIONS.AnimateSequence( f72_arg0, "HideClock" )
		end
		local f72_local5 = function ( f78_arg0 )
			if f72_local2.bannerText then
				ACTIONS.AnimateSequence( f72_arg0, "ShowBanner" )
			end
			if f72_local2.showClock then
				ACTIONS.AnimateSequence( f72_arg0, "ShowClock" )
			end
		end
		
		local f72_local6 = function ()
			ACTIONS.AnimateSequence( f72_arg0, "HideBanner" )
			ACTIONS.AnimateSequence( f72_arg0, "HideClock" )
		end
		
		if f72_local2.bannerText then
			f72_arg0.BannerText:setText( Engine.Localize( f72_local2.bannerText ) )
			ACTIONS.AnimateSequence( f72_arg0, "ShowBanner" )
			f72_arg0:addEventHandler( "button_over", f72_local6 )
			f72_arg0:addEventHandler( "button_up_mouse", f72_local5 )
			f72_arg0:addEventHandler( "button_down", f72_local5 )
			f72_arg0:addEventHandler( "button_up", f72_local5 )
		else
			f72_arg0.BannerText:setText( "" )
			ACTIONS.AnimateSequence( f72_arg0, "HideBanner" )
		end
		if f72_local2.playlistBonuses and #f72_local2.playlistBonuses > 0 then
			ACTIONS.AnimateSequence( f72_arg0, "ShowDoubleXPSignposting" )
			if #f72_local2.playlistBonuses > 1 then
				f72_arg0.DoubleXPSignpostingItem:SetLoopingDoubleXPTypes( f72_local2.playlistBonuses )
			elseif Engine.IsZombiesMode() then
				f72_arg0.DoubleXPSignpostingItem:SetDoubleXPTypeZM( f72_local2.playlistBonuses[1], DoubleXPUtils.Animation.ActiveIconOnly )
			else
				f72_arg0.DoubleXPSignpostingItem:SetDoubleXPType( f72_local2.playlistBonuses[1], DoubleXPUtils.Animation.ActiveIconOnly )
			end
		elseif f72_local2.showDoubleXPSignposting then
			assert( f72_arg0.DoubleXPSignpostingItem )
			if #f72_arg0.DoubleXPSignpostingItem:SetDoubleXPTypeByDVars() > 0 then
				ACTIONS.AnimateSequence( f72_arg0, "ShowDoubleXPSignposting" )
			else
				ACTIONS.AnimateSequence( f72_arg0, "HideDoubleXPSignposting" )
			end
		else
			ACTIONS.AnimateSequence( f72_arg0, "HideDoubleXPSignposting" )
		end
		f0_local45( f72_local4 )
		if f72_local2.playlistImage then
			f72_local4.playlistIcon:setImage( RegisterMaterial( f72_local2.playlistImage ) )
			f72_local4.playlistIcon:setAlpha( 1 )
		else
			f72_local4.playlistIcon:setAlpha( 0 )
		end
		f72_arg0._gridPosition = {
			x = f72_arg1,
			y = f72_arg2
		}
		if f72_local2.initFunc then
			f72_local2.initFunc( f72_arg0 )
		end
	end
	,
	ACTION_StartPrivateMatch = f0_local8,
	ACTION_StartRankedMatch = f0_local9,
	ACTION_StartGameBattles = f0_local10,
	ACTION_ReplayTraining = f0_local12,
	ACTION_TogglePrelobby = f0_local13,
	ACTION_StartSinglePlayer = f0_local14,
	ACTION_StartZombies = f0_local16,
	ACTION_StartLocalPlay = f0_local19,
	ACTION_GoToMultiplayer = f0_local17,
	ACTION_LocalPlayerJoined = f0_local18,
	FUNCTOR_GetDefaultFocusGrid = f0_local20,
	FUNCTOR_IsFocusable = function ( f80_arg0, f80_arg1, f80_arg2 )
		return f0_local31( LUI.FlowManager.GetScopedData( f80_arg0 ), f80_arg1, f80_arg2 ) ~= nil
	end
	,
	ACTION_RefreshGridDisplay = f0_local35,
	ACTION_RefreshExplicitButtons = f0_local37,
	ACTION_ShiftGrid = f0_local39,
	CONDITION_IsExplicitButtonsLayout = function ( f81_arg0 )
		local f81_local0 = LUI.FlowManager.GetScopedData( f81_arg0 )
		return f81_local0.findGameMenu.layoutStyle == f0_local1.ExplicitButtons
	end
	
}
