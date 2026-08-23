local dedicatedParty = {}

function dedicatedParty.GetMapName()
	if Lobby.GetDedicatedPartyMapName then
		return Lobby.GetDedicatedPartyMapName()
	end

	return nil
end

function dedicatedParty.GetGameType()
	if Lobby.GetS2xMapGameType then
		local gametype = Lobby.GetS2xMapGameType()
		if gametype and gametype ~= "" then
			return gametype
		end
	end

	if Lobby.GetDedicatedPartyGameType then
		return Lobby.GetDedicatedPartyGameType()
	end

	return nil
end

function dedicatedParty.GetGameTypeName()
	local gametype = dedicatedParty.GetGameType()
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

function dedicatedParty.GetMaxPlayers()
	if Lobby.GetDedicatedPartyMaxPlayers then
		local maxPlayers = Lobby.GetDedicatedPartyMaxPlayers()
		if maxPlayers and maxPlayers > 0 then
			return maxPlayers
		end
	end

	return nil
end

return dedicatedParty
