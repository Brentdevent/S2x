
local function GetDedicatedPartyGameType()
	if Lobby.GetDedicatedPartyGameType then
		return Lobby.GetDedicatedPartyGameType()
	end

	return nil
end

local function GetDedicatedPartyGameTypeName()
	local gametype = GetDedicatedPartyGameType()
	if gametype and gametype ~= "" then
		return Engine.TableLookup(
			GameTypesTable.File,
			GameTypesTable.Cols.Ref,
			gametype,
			GameTypesTable.Cols.Name
		)
	end

	return nil
end

local function GetDedicatedPartyMaxPlayers()
	if Lobby.GetDedicatedPartyMaxPlayers then
		local maxPlayers = Lobby.GetDedicatedPartyMaxPlayers()
		if maxPlayers and maxPlayers > 0 then
			return maxPlayers
		end
	end

	return nil
end

local function IsVisibleDedicatedPartyMember( xuid, controller )
	if not GetDedicatedPartyMaxPlayers() or not xuid then
		return true
	end

	controller = controller or Engine.GetFirstActiveController()
	local members = DataSources and DataSources.inFrontend and
		DataSources.inFrontend.MP and DataSources.inFrontend.MP.lobby and
		DataSources.inFrontend.MP.lobby.members or nil
	if not members then
		return true
	end

	local visibleCount = members:GetCountValue( controller )
	local expectedCount = Lobby.GetDedicatedPartyMemberCount and
		Lobby.GetDedicatedPartyMemberCount() or visibleCount

	-- Let the native model finish rebuilding before filtering a newly joined member.
	if visibleCount == 0 and expectedCount > 0 then
		return true
	end

	for index = 0, visibleCount - 1 do
		local member = members[index]
		if member and member.xuid and member.xuid:GetValue( controller ) == xuid then
			return true
		end
	end

	return false
end

if Lobby.GetCurrentMemberCount and not Lobby.S2xStockGetCurrentMemberCount then
	Lobby.S2xStockGetCurrentMemberCount = Lobby.GetCurrentMemberCount
end

if Lobby.S2xStockGetCurrentMemberCount then
	local stockGetCurrentMemberCount = Lobby.S2xStockGetCurrentMemberCount
	Lobby.GetCurrentMemberCount = function( state, ... )
		if state == Lobby.MemberListStates.Lobby and Lobby.GetDedicatedPartyMemberCount then
			local count = Lobby.GetDedicatedPartyMemberCount()
			if count and count >= 0 then
				return count
			end
		end

		return stockGetCurrentMemberCount( state, ... )
	end
end

if GetPartyMaxPlayers and not S2xStockGetPartyMaxPlayers then
	S2xStockGetPartyMaxPlayers = GetPartyMaxPlayers
end

if S2xStockGetPartyMaxPlayers then
	GetPartyMaxPlayers = function( ... )
		local maxPlayers = GetDedicatedPartyMaxPlayers()
		if maxPlayers then
			return maxPlayers
		end

		return S2xStockGetPartyMaxPlayers( ... )
	end
end

if Character_Scene and Character_Scene.HandleUpdateVLLoadout and
	not Character_Scene.S2xStockHandleUpdateVLLoadout then
	Character_Scene.S2xStockHandleUpdateVLLoadout = Character_Scene.HandleUpdateVLLoadout
end

if Character_Scene and Character_Scene.S2xStockHandleUpdateVLLoadout then
	Character_Scene.HandleUpdateVLLoadout = function( element, event )
		if event and event.loadouts and GetDedicatedPartyMaxPlayers() then
			local filteredLoadouts = {}
			for _, loadout in ipairs( event.loadouts ) do
				if IsVisibleDedicatedPartyMember( loadout.xuid, event.controller ) then
					table.insert( filteredLoadouts, loadout )
				end
			end

			event.loadouts = filteredLoadouts
		end

		return Character_Scene.S2xStockHandleUpdateVLLoadout( element, event )
	end

	-- SetupEventHandlers captures the original callback by value. Rebind the root
	-- event so future native loadout updates pass through the dedicated filter.
	local root = Engine.GetLuiRoot()
	if root then
		root:registerEventHandler(
			"update_vl_loadout",
			Character_Scene.HandleUpdateVLLoadout
		)
	end
end

function S2xRefreshDedicatedPartyPresentation()
	local memberCount = Lobby.GetDedicatedPartyMemberCount and
		Lobby.GetDedicatedPartyMemberCount() or nil
	local maxPlayers = GetDedicatedPartyMaxPlayers()
	local root = Engine.GetLuiRoot()
	if memberCount and memberCount >= 0 and maxPlayers and root and root.flowManager then
		local menuInfo = LUI.FlowManager.GetTopMenuInfo( root.flowManager.menuInfoStack )
		local page = menuInfo and menuInfo.menu and menuInfo.menu.CurrentMenuPage or nil
		if page and page.LobbyPlayerCount then
			page.LobbyPlayerCount:setText( memberCount .. "/" .. maxPlayers )
		end
	end

	if not GetDedicatedPartyMaxPlayers() or not avatarData or not Character_Scene then
		return
	end

	local avatarLimit = maxVLClients or 18
	for index = 1, avatarLimit do
		local avatar = avatarData[index]
		if avatar and avatar.xuid and avatar.xuid ~= NoXuid and
			not IsVisibleDedicatedPartyMember( avatar.xuid ) then
			local leavingXuid = avatar.xuid
			if avatar.avatarHandle then
				CharacterScene.Show( avatar.avatarHandle, false )
				avatar.showing = false
			end

			if Character_Scene.HandleVLClientsLeaving then
				Character_Scene.HandleVLClientsLeaving( Engine.GetLuiRoot(), {
					leavingXuids = { leavingXuid }
				} )
			end
		end
	end

	if CharacterScene.RequestUpdateVLLoadout then
		CharacterScene.RequestUpdateVLLoadout( Engine.GetFirstActiveController() )
	end
end

if not Lobby.S2xStockGameTypeName then
	Lobby.S2xStockGameTypeName = Lobby.GameTypeName
end

local stockGameTypeName = Lobby.S2xStockGameTypeName
Lobby.GameTypeName = function( ... )
	local displayName = GetDedicatedPartyGameTypeName()
	if displayName and displayName ~= "" then
		return displayName
	end

	return stockGameTypeName( ... )
end


if Lobby.GameTypeNameAbbreviated and not Lobby.S2xStockGameTypeNameAbbreviated then
	Lobby.S2xStockGameTypeNameAbbreviated = Lobby.GameTypeNameAbbreviated
end

if Lobby.S2xStockGameTypeNameAbbreviated then
	local stockGameTypeNameAbbreviated = Lobby.S2xStockGameTypeNameAbbreviated
	Lobby.GameTypeNameAbbreviated = function( ... )
		local displayName = GetDedicatedPartyGameTypeName()
		if displayName and displayName ~= "" then
			return displayName
		end

		return stockGameTypeNameAbbreviated( ... )
	end
end


if GameX and GameX.GetGameMode and not GameX.S2xStockGetGameMode then
	GameX.S2xStockGetGameMode = GameX.GetGameMode
end

if GameX and GameX.S2xStockGetGameMode then
	local stockGetGameMode = GameX.S2xStockGetGameMode
	GameX.GetGameMode = function( ... )
		local gametype = GetDedicatedPartyGameType()
		if gametype and gametype ~= "" then
			return gametype
		end

		return stockGetGameMode( ... )
	end
end


if GetGameModeName and not S2xStockGetGameModeName then
	S2xStockGetGameModeName = GetGameModeName
end

if S2xStockGetGameModeName then
	GetGameModeName = function( ... )
		local gametype = GetDedicatedPartyGameType()
		if gametype and gametype ~= "" then
			return Engine.Localize( Lobby.GameTypeName() )
		end

		return S2xStockGetGameModeName( ... )
	end
end


function CanChangeTeam()
	local f7_local0 = GameX.GetGameMode()
	local f7_local2 = Engine.TableLookup( GameTypesTable.File, GameTypesTable.Cols.Ref, f7_local0, GameTypesTable.Cols.TeamChoice ) == "1"
	local f7_local3 = CONDITIONS.IsScorestreakTraining()
	local f7_local4
	
	if f7_local2 == true and (Engine.GetDvarBool( "3193" )) and not Broadcaster.IsBroadcaster() and not GameBattlesUtils.IsActive() then
		f7_local4 = not f7_local3
	else
		f7_local4 = false
	end
	
	return f7_local4
end
