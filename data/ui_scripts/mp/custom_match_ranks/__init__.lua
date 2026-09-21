-- This patch is needed in both the frontend lobby and the in-game scoreboard.
if game:issingleplayer() or Engine.IsZombiesMode() then
	return
end

CustomMatch = CustomMatch or {}
if CustomMatch.RanksInstalled then
	return
end
CustomMatch.RanksInstalled = true

local builders = LUI.MenuBuilder.m_types_build
local is_private_match = CONDITIONS.IsPrivateMatch
local scoreboard_rows = setmetatable( {}, { __mode = "k" } )
local rank_controls = setmetatable( {}, { __mode = "k" } )
local building_member = false

local function is_custom_match()
	-- Native bindings are installed after UI scripts; do not capture them here.
	return Lobby.IsCustomMatch and Lobby.IsCustomMatch()
end

CONDITIONS.IsPrivateMatch = function ( element, ... )
	if type( element ) == "table" or type( element ) == "userdata" then
		-- Grids rename their children after construction. Keep the identity for
		-- ScoreboardRow_uc's later rank-text updates (including the AAR).
		if element.id == "ScoreboardRow" then
			scoreboard_rows[element] = true
		end
		if (scoreboard_rows[element] or (building_member and element.id == "member_list_item"))
			and is_custom_match() then
			if scoreboard_rows[element] and Lobby.GetCustomMatchProgression() then
				return false
			end
			if building_member and element.id == "member_list_item" then
				return false
			end
		end
	end
	return is_private_match( element, ... )
end

local function follow_progression( control, row )
	if not control then
		return
	end
	local set_alpha = control.setAlpha
	local requested_alpha = control:getAlpha()
	local function visible()
		if is_custom_match() then
			return Lobby.GetCustomMatchProgression()
		end
		return not is_private_match( row )
	end
	control.setAlpha = function ( self, alpha, ... )
		-- Preserve stock grid_cell_empty/populated behavior. A toggle must not
		-- reveal empty rows, nor may repopulating a row reveal disabled ranks.
		requested_alpha = alpha
		return set_alpha( self, visible() and alpha or 0, ... )
	end
	control.RefreshCustomMatchRank = function ( self )
		set_alpha( self, visible() and requested_alpha or 0, 0 )
	end
	-- Store no strong reference to the row/closure in the registry, so closed
	-- lobby menus can be collected by Lua 5.1's weak-key tables.
	rank_controls[control] = true
	control:RefreshCustomMatchRank()
end

function CustomMatch.RefreshRanks()
	for control in pairs( rank_controls ) do
		control:RefreshCustomMatchRank()
	end
end

local member_builder = builders.member_list_item
if member_builder then
	builders.member_list_item = function ( ... )
		if not is_custom_match() then
			return member_builder( ... )
		end
		-- These two rank widgets are normally omitted entirely in private
		-- lobbies. Build their stock model subscriptions even when disabled so
		-- enabling progression later does not need to recreate the whole lobby.
		local previous = building_member
		building_member = true
		local ok, row = pcall( member_builder, ... )
		building_member = previous
		if not ok then
			error( row, 0 )
		end
		follow_progression( row.PlayerRankText, row )
		follow_progression( row.PlayerRankIcon, row )
		return row
	end
end
