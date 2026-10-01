if not game:ismultiplayer() then
	return
end

local NotifyServer_og = Engine.NotifyServer

local function is_custom_match_leave()
	local frontend = Engine.InFrontend()
	local zombies = Engine.IsZombiesMode()
	local host = Lobby.IsGameHost()
	if frontend or zombies or not host then
		return false
	end

	local custom = Lobby.IsCustomMatch and Lobby.IsCustomMatch()
	local systemlink = CONDITIONS.IsSystemLink()
	if not ( custom or systemlink ) then
		return false
	end

	local root = Engine.GetLuiRoot()
	local stack = root and root.flowManager and root.flowManager.menuInfoStack
	if not stack then
		return false
	end

	local pause_menu = false
	local confirmation = false
	for _, menu_info in pairs( stack ) do
		if menu_info.name == "pause_menu_leave" then
			return true
		end
		pause_menu = pause_menu or menu_info.name == "mp_pause_menu"
		confirmation = confirmation or menu_info.name == "yesno_popmenu"
	end

	return pause_menu and confirmation
end

local ExecFirstClient_og = Engine.ExecFirstClient
Engine.ExecFirstClient = function ( command, ... )
	-- host's confirmed leave in a pm uses hostmigration_start for some reason
	if command == "hostmigration_start" and is_custom_match_leave() then
		return ExecFirstClient_og( "disconnect", ... )
	end

	return ExecFirstClient_og( command, ... )
end

Engine.NotifyServer = function ( notification, ... )
	if notification == "end_game" and is_custom_match_leave() then
		Engine.ExecFirstClient( "disconnect" )
		return
	end

	return NotifyServer_og( notification, ... )
end
