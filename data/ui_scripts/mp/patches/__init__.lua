
if not Lobby.GameTypeNamePatched then
	Lobby.GameTypeNamePatched = true
	local stockGameTypeName = Lobby.GameTypeName

	Lobby.GameTypeName = function( ... )
		local gametype = Lobby.GetDedicatedPartyGameType and Lobby.GetDedicatedPartyGameType()
		if gametype and gametype ~= "" then
			local displayName = Engine.TableLookup(
				GameTypesTable.File,
				GameTypesTable.Cols.Ref,
				gametype,
				GameTypesTable.Cols.Name
			)

			if displayName and displayName ~= "" then
				return displayName
			end
		end

		return stockGameTypeName( ... )
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
