local custom = CustomMatch

-- These are frontend/training sessions, not private MP game modes. All game
-- modes and maps themselves come from the installed stock definitions.
local nonMatchModes = { hub = true, vlobby = true, zombies = true, scorestreak_training = true }
local catalog
local modes, maps, explicitlySupported

local function HasToken( text, token )
	return (" " .. (text or "") .. " "):find( " " .. token .. " ", 1, true ) ~= nil
end

function custom.RefreshCatalog()
	if not Lobby.GetCustomMatchCatalog then
		return false
	end
	catalog = Lobby.GetCustomMatchCatalog()
	modes, maps, explicitlySupported = {}, {}, {}
	for _, ref in ipairs( catalog.gametypes ) do
		if not nonMatchModes[ref] then
			modes[ref] = true
		end
	end
	for _, map in ipairs( catalog.maps ) do
		maps[map.map] = map
		for token in (map.gametype or ""):gmatch( "%S+" ) do
			explicitlySupported[token] = true
		end
	end
	-- private_lobby also validates its current mode against this list on entry.
	local list = {}
	for _, ref in ipairs( catalog.gametypes ) do
		if modes[ref] and #custom.GetMaps( ref ) > 0 then
			table.insert( list, ref )
		end
	end
	Cac.GameModes.Data.Standard.List = list
	return true
end

function custom.Supports( mapName, ref )
	local map = maps and maps[mapName]
	if not map or not map.available or not modes[ref] then
		return false
	end
	-- Stock MapCheck separates these map families before testing standard maps.
	-- Dogfight's arena gametype field incorrectly contains ordinary MP modes.
	if mapName:match( "_dogfight$" ) then
		return ref == "dogfight" or ref == "dogfight_ffa"
	elseif ref == "dogfight" or ref == "dogfight_ffa" then
		return false
	elseif mapName:match( "^mp_raid_" ) then
		return ref == "raid"
	elseif ref == "raid" then
		return false
	end
	if not HasToken( map.gametype, "dm" ) or not HasToken( map.gametype, "war" ) then
		return false -- hub, training and other non-gameplay arenas
	end
	-- This is a promotional alias of mp_wolfslair, not an extra arena. Stock
	-- development gates also hide shipped maps (House, Sandbox and Dogfight);
	-- use their arena definitions and installed files instead of those gates.
	if mapName == "mp_wolfslair_free" then
		return false
	end
	-- The old arena catalog predates several shipped modes (including CTF and
	-- Hordepoint). Stock MapCheck allows these on standard maps. When a mode
	-- does have explicit arena support (e.g. control), honor that restriction.
	return not explicitlySupported[ref] or HasToken( map.gametype, ref )
end

function custom.GetMaps( ref )
	local result = {}
	for _, map in ipairs( catalog.maps ) do
		if custom.Supports( map.map, ref ) then
			table.insert( result, map )
		end
	end
	return result
end

local function IsPrivateMP()
	return Engine.GetGameIsPrivateMatch() and not Engine.IsZombiesMode()
end

local function IsCustomMatchGuest()
	return Lobby.IsCustomMatch and Lobby.IsCustomMatch() and not Lobby.IsGameHost()
end

local stockLimits = GetPrivateMatchPlayerLimits
GetPrivateMatchPlayerLimits = function ( ref )
	if IsPrivateMP() and Lobby.GetCustomMatchPlayerLimit then
		if IsCustomMatchGuest() then
			return Engine.GetPartyMaxPlayers()
		end
		return Lobby.GetCustomMatchPlayerLimit()
	end
	return stockLimits( ref )
end

local stockPartyMax = GetPartyMaxPlayers
GetPartyMaxPlayers = function ( ... )
	if IsPrivateMP() then
		return Engine.GetPartyMaxPlayers()
	end
	return stockPartyMax( ... )
end

local stockUpdate = UpdatePrivateMatchMaxPlayers
UpdatePrivateMatchMaxPlayers = function ( ... )
	-- A joined host owns capacity, even when it is an older 12-slot lobby.
	if IsPrivateMP() and IsCustomMatchGuest() then
		return
	end
	if IsPrivateMP() and Lobby.IsGameHost() and Lobby.ApplyCustomMatchPlayerLimit then
		Lobby.ApplyCustomMatchPlayerLimit()
		if Lobby.IsInPrivateParty() and Lobby.IsPrivatePartyHost() then
			UpdatePrivatePartyMaxPlayers( Lobby.GetCustomMatchPlayerLimit() )
		end
	end
	return stockUpdate( ... ) -- SetGamePartyMaxPlayers + stock session slot update
end

local stockBotRight = BotButtonRightLimit12
BotButtonRightLimit12 = function ( ... )
	if IsPrivateMP() then
		return FFABotButtonRight( ... )
	end
	return stockBotRight( ... )
end

local function EnsureMap( ref, preferred )
	if not Lobby.IsGameHost() then
		-- Stock PostLoad also resets Dogfight for guests. Preserve the received
		-- selection locally, without choosing a replacement or publishing it.
		if preferred ~= Engine.GetPartyMapName() then
			Engine.SetPartyMapName( preferred )
		end
		return custom.Supports( preferred, ref )
	end
	if MatchRules.IsUsingCustomMapRotation() then
		for index = Lobby.NumMapsInRotation() - 1, 0, -1 do
			local name = Lobby.GetMapInRotation( index )
			if not custom.Supports( name, ref ) then
				Lobby.IncludeMapInRotation( name, false )
			end
		end
		if Lobby.NumMapsInRotation() > 0 then
			-- The native rotation selects the next map on return to the lobby.
			-- Rebuilding setup must not rewind that selection to the first entry.
			if custom.Supports( preferred, ref ) and Lobby.IsMapInRotation( preferred ) then
				Engine.SetPartyMapName( preferred )
			else
				-- Repair the native cursor as well as the visible party map.
				Lobby.IncludeMapInRotation( Lobby.GetMapInRotation( 0 ), true )
			end
			return true
		end
		MatchRules.SetUsingCustomMapRotation( false )
	end
	if custom.Supports( preferred, ref ) then
		Engine.SetPartyMapName( preferred )
		return true
	end
	local available = custom.GetMaps( ref )
	if #available == 0 then
		return false
	end
	Engine.SetPartyMapName( available[1].map )
	return true
end

local function RefreshMenu( menu, controller )
	menu.Nested_Button_List:InitializeList( controller,
		menu:GetBuildMatchSetupOptionsFunc( controller ), false )
end

local function SelectMode( menu, controller, ref )
	if not Lobby.IsGameHost() then return end
	custom.RefreshCatalog()
	if #custom.GetMaps( ref ) == 0 then
		return
	end
	local previous = GetCurrentGameType()
	FixTeamLimitsAndDifficultiesIfNecessary( previous, ref )
	if not MatchRules.SetData( "gametype", ref ) then
		return
	end
	MatchRules.SetData( "modePrefix", "" )
	Competitive.SetCompetitiveMatch( false )
	if Engine.GetSplitScreen() then
		Engine.ExecNow( MPConfig.default_splitscreen, controller )
	elseif Engine.GetSystemLink() then
		Engine.ExecNow( MPConfig.default_systemlink, controller )
	else
		Engine.ExecNow( MPConfig.default_xboxlive, controller )
	end
	Engine.SetPartyGameType( ref )
	MatchRules.SetUsingCustomMapRotation( false )
	EnsureMap( ref, Engine.GetPartyMapName() )
	if ref == "raid" then
		ApplyRaidRecipeOverrides()
	end
	UpdatePrivateMatchMaxPlayers()
	RefreshBotLimits( ref )
	Engine.ExecNow( "xupdatepartystate" )
	RefreshMenu( menu, controller )
end

local function Preview( menu, image, title, description )
	if image and image ~= "" then
		menu.ShowcaseImage:setImage( RegisterMaterial( MODIFIERS.CacheIconMaterial( image ) ), 0 )
	end
	menu.ShowcaseTitle:setText( Engine.ToUpperCase( Engine.Localize( title ) ), 0 )
	menu.ShowcaseSubtitle:setText( Engine.Localize( description or "" ), 0 )
	ACTIONS.AnimateSequence( menu, "ShowcaseImageAndTitle" )
end

local function ModeButtons( menu, controller )
	local result = {}
	for _, ref in ipairs( Cac.GameModes.Data.Standard.List ) do
		local name = Engine.TableLookup( GameTypesTable.File, GameTypesTable.Cols.Ref, ref, GameTypesTable.Cols.Name )
		local desc = Engine.TableLookup( GameTypesTable.File, GameTypesTable.Cols.Ref, ref, GameTypesTable.Cols.Desc )
		local icon = GetGameTypeIcon( ref )
		table.insert( result, {
			text = name ~= "" and name or ref,
			disabledFunc = function () return not Lobby.IsGameHost() end,
			defaultFocus = ref == GetCurrentGameType(),
			focusFunc = function () Preview( menu, icon, name, desc ) end,
			actionFunc = function () SelectMode( menu, controller, ref ) end
		} )
	end
	return result
end

local function MapButtons( menu, controller )
	custom.RefreshCatalog()
	local ref = GetCurrentGameType()
	local result = {}
	for _, map in ipairs( custom.GetMaps( ref ) ) do
		table.insert( result, {
			elementType = NestedButtonListElementTypes.CheckboxButton,
			text = map.longname,
			disabledFunc = function () return not Lobby.IsGameHost() end,
			initiallyChecked = Lobby.IsMapInRotation( map.map ),
			defaultFocus = map.map == Engine.GetPartyMapName(),
			handlers = {
				content_refresh = function ( element )
					-- The flat list bypasses stock category focus handlers. Sync
					-- every row on creation/reentry and after the stock F1 toggle.
					element:processEvent( {
						name = "set_checkbox_visible",
						isVisible = ref ~= "raid" and MatchRules.IsUsingCustomMapRotation()
					} )
				end
			},
			focusFunc = function ()
				Preview( menu, map.mapimage, map.longname, map.description )
				if ref ~= "raid" and Lobby.IsInPrivateParty() and Lobby.IsPrivatePartyHost() then
					menu.ButtonHelperBar:ShowButtonsWithTag( "game setup menu map button" )
				end
			end,
			actionFunc = function ()
				if not Lobby.IsGameHost() then return end
				custom.RefreshCatalog()
				if custom.Supports( map.map, GetCurrentGameType() ) then
					if MatchRules.IsUsingCustomMapRotation() and ref ~= "raid"
						and Lobby.IsInPrivateParty() and Lobby.IsPrivatePartyHost() then
						-- Native rotation updates both its cursor and the party map.
						-- Overwriting the map here would leave those out of sync.
						Lobby.IncludeMapInRotation( map.map, not Lobby.IsMapInRotation( map.map ) )
					else
						Engine.SetPartyMapName( map.map )
					end
					Engine.ExecNow( "xupdatepartystate" )
					if ref == "raid" then ApplyRaidRecipeOverrides() end
					menu.Nested_Button_List:RefreshAllContent()
					if not MatchRules.IsUsingCustomMapRotation() then
						menu.Nested_Button_List:LeaveSublist( true )
					end
				end
			end
		} )
	end
	return result
end

local builders = LUI.MenuBuilder.m_types_build
local stockBuilder = builders.MP_Game_Setup_Menu
builders.MP_Game_Setup_Menu = function ( builder, properties )
	if not IsPrivateMP() or not custom.RefreshCatalog() then
		return stockBuilder( builder, properties )
	end
	local preferred = Engine.GetPartyMapName()
	local menu = stockBuilder( builder, properties )
	local controller = properties and properties.controllerIndex or Engine.GetFirstActiveController()
	-- Stock PostLoad rejects Dogfight outside development builds. Restore the
	-- validated selection without reloading stock modules or enabling dev UI.
	EnsureMap( GetCurrentGameType(), preferred )
	local stockOptions = menu.GetBuildMatchSetupOptionsFunc
	menu.GetBuildMatchSetupOptionsFunc = function ( self, index )
		EnsureMap( GetCurrentGameType(), Engine.GetPartyMapName() )
		local build = stockOptions( self, index )
		return function ()
			local options = build()
			local stockModes = options.elements[1].buildElementsFunc
			options.elements[1].buildElementsFunc = function ()
				local groups = stockModes()
				groups[1].buildElementsFunc = function () return ModeButtons( menu, controller ) end
				return groups
			end
			options.elements[2].elements = nil
			options.elements[2].buildElementsFunc = function () return MapButtons( menu, controller ) end
			return options
		end
	end
	RefreshMenu( menu, controller )
	return menu
end
