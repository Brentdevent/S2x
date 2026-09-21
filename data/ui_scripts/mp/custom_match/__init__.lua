if game:issingleplayer() or not Engine.InFrontend() or Engine.IsZombiesMode() then
	return
end

CustomMatch = CustomMatch or {}

local builders = LUI.MenuBuilder.m_types_build
local original = builders.MP_Game_Setup_Menu
if not original or CustomMatch.ProgressionInstalled then
	return
end
CustomMatch.ProgressionInstalled = true

-- Stock private_lobby's PostLoad hard-codes the private schema when migrating
-- old division loadouts, but these helpers read the currently selected stats
-- group. With online loadouts that schema does not exist, aborting menu creation
-- and sending navigation back to News. Migrate the selected bank instead.
for _, name in ipairs( { "CleanLoadoutsForCavalryNerf", "CleanLoadoutsForDivisionsOverhaul" } ) do
	local clean = Cac[name]
	Cac[name] = function ( controller, location, ... )
		if location == "privateMatchCustomClasses" and Lobby.IsCustomMatch
			and Lobby.IsCustomMatch() and Lobby.GetCustomMatchProgression() then
			location = "customClasses"
		end
		return clean( controller, location, ... )
	end
end

local function refresh_loadouts()
	for controller = 0, Engine.GetMaxControllerCount() - 1 do
		if Engine.HasActiveLocalClient( controller ) then
			DataSources.Shared.CacLoadout:UpdateAggregateValue( controller )
		end
	end
	if CustomMatch.RefreshRanks then
		CustomMatch.RefreshRanks()
	end
end

-- A guest may be editing a weapon when the host changes banks. Discard that
-- editor's captured data sources before accepting more input. Rebuild only the
-- existing lobby instance; never reload its module or leave the network party.
function CustomMatch.OnProgressionChanged()
	if not Engine.InFrontend() or not Lobby.IsCustomMatch() then
		return
	end
	refresh_loadouts()
	local root = Engine.GetLuiRoot()
	if root and LUI.FlowManager.IsMenuInStack( root, "private_lobby" ) then
		LUI.FlowManager.RequestCloseAllUntil(
			root, "private_lobby", false, true, Engine.GetFirstActiveController() )
		Engine.RebuildTopMenu()
	end
end

local function find_game_rules( elements )
	local rules_title = Engine.ToUpperCase( Engine.Localize( "MPUI_GAME_RULES" ) )
	for _, element in ipairs( elements ) do
		if element.buildElementsFunc and
			Engine.ToUpperCase( Engine.Localize( element.text ) ) == rules_title then
			return element
		end
	end
end

local function add_progression_rule( menu, category )
	local function toggle()
		if Lobby.SetCustomMatchProgression( not Lobby.GetCustomMatchProgression() ) then
			refresh_loadouts()
			menu.Nested_Button_List:RefreshAllContent()
		end
	end
	local progression = {
		elementType = NestedButtonListElementTypes.ScrollableButton,
		text = "Progression",
		isTextLocalized = true,
		buttonDisplayFunc = function ()
			return Engine.Localize( Lobby.GetCustomMatchProgression()
				and "LUA_MENU_ENABLED" or "LUA_MENU_DISABLED" )
		end,
		buttonLeftFunc = toggle,
		buttonRightFunc = toggle,
		disabledFunc = function () return not Lobby.IsGameHost() end,
		focusFunc = function ()
			category.focusFunc()
			menu.ShowcaseTitle:setText( "PROGRESSION", 0 )
			menu.ShowcaseSubtitle:setText(
				"Earn XP with your online Soldier loadouts, or disable progression " ..
				"to use separate private match loadouts.", 0 )
			ACTIONS.AnimateSequence( menu, "ShowcaseTitle" )
			for _, element in ipairs( { menu.ShowcaseTitle, menu.ShowcaseSubtitle } ) do
				element:setLeft( 876 * _1080p, 0 )
				element:setRight( 1610 * _1080p, 0 )
			end
		end
	}
	local build_rules = category.buildElementsFunc
	category.buildElementsFunc = function ( ... )
		-- Stock retains this array; copy it so reopening never duplicates the row.
		local rules = { unpack( build_rules( ... ) ) }
		table.insert( rules, progression )
		return rules
	end
end

-- Decorate the built instance. Requiring the stock private-lobby modules again
-- resets their navigation registrations and can send Custom Match back to News.
builders.MP_Game_Setup_Menu = function ( builder, arguments )
	local menu = original( builder, arguments )
	if not Lobby.IsCustomMatch or not Lobby.IsCustomMatch() then
		return menu
	end

	local original_options = menu.GetBuildMatchSetupOptionsFunc
	menu.GetBuildMatchSetupOptionsFunc = function ( self, controller )
		local build_options = original_options( self, controller )
		return function ()
			local options = build_options()
			-- Both the category list and its rule lists are built lazily.
			local root_rules = find_game_rules( options.elements )
			if root_rules then
				local build_categories = root_rules.buildElementsFunc
				root_rules.buildElementsFunc = function ( ... )
					local categories = build_categories( ... )
					local game_rules = find_game_rules( categories )
					if game_rules then
						add_progression_rule( menu, game_rules )
					end
					return categories
				end
			end
			return options
		end
	end

	local controller = arguments and arguments.controllerIndex
	if controller == nil then
		controller = LUI.FlowManager.GetScopedData( menu ).exclusiveControllerIndex
	end
	refresh_loadouts()
	local show_parent
	if Engine.IsPC() then
		show_parent = false
	end
	menu.Nested_Button_List:InitializeList(
		controller, menu:GetBuildMatchSetupOptionsFunc( controller ), show_parent )
	return menu
end
