if not Cac or not Cac.GetCustomClassLoc then
	return
end

local function uses_online_loadouts()
	return Lobby.IsCustomMatch and Lobby.IsCustomMatch() and Lobby.GetCustomMatchProgression()
end

local GetCustomClassLoc_og = Cac.GetCustomClassLoc
Cac.GetCustomClassLoc = function ( ... )
	local location = GetCustomClassLoc_og( ... )
	if location == "privateMatchCustomClasses" and uses_online_loadouts() then
		return "customClasses"
	end
	return location
end

if Cac.ValidateMLGLoadouts then
	local ValidateMLGLoadouts_og = Cac.ValidateMLGLoadouts
	Cac.ValidateMLGLoadouts = function ( ... )
		if uses_online_loadouts() then
			return
		end
		return ValidateMLGLoadouts_og( ... )
	end
end
