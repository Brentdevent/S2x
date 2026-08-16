local dedicatedParty = require( "dedicated_party" )
local GetDedicatedPartyMaxPlayers = dedicatedParty.GetMaxPlayers

local function IsVisibleDedicatedPartyMember( xuid, controller )
	if not GetDedicatedPartyMaxPlayers() or not xuid then
		return true
	end

	controller = controller or Engine.GetFirstActiveController()
	if xuid == Engine.GetXUIDByController( controller ) then
		return true
	end

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
	if S2xRefreshDedicatedLobbyPresentation then
		S2xRefreshDedicatedLobbyPresentation()
	end

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
