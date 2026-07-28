local dedicatedParty = require( "dedicated_party" )
local GetDedicatedPartyGameType = dedicatedParty.GetGameType
local GetDedicatedPartyGameTypeName = dedicatedParty.GetGameTypeName

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
