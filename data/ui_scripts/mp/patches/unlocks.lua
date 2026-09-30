if game:issingleplayer() or not Engine.InFrontend() then
	return
end

local function get_controller_index( element, properties )
	if properties and properties.controllerIndex then
		return properties.controllerIndex
	end

	local scoped_data = LUI.FlowManager.GetScopedData( element )
	if scoped_data and scoped_data.exclusiveControllerIndex then
		return scoped_data.exclusiveControllerIndex
	end

	return Engine.GetFirstActiveController()
end

local function get_toggle_text( dvar_name )
	return function ()
		if Engine.GetDvarBool( dvar_name ) then
			return Engine.Localize( "@LUA_MENU_ENABLED" )
		end

		return Engine.Localize( "@LUA_MENU_DISABLED" )
	end
end

local function toggle_dvar( dvar_name )
	return function ()
		Engine.SetDvarBool( dvar_name, not Engine.GetDvarBool( dvar_name ) )
	end
end

local function open_unlock_confirmation( element, controller, command, warning )
	LUI.FlowManager.RequestAddMenu( element, "notification_modal", true, controller, false, {
		titleText = Engine.Localize( "@MENU_WARNING" ),
		descText = warning,
		icon = nil,
		modalType = ModalUtils.NotificationModalType.GeneralNotifications,
		accept_func = function ()
			Engine.Exec( command .. " confirm" )
		end,
		cancel_func = function ()
		end,
		choices = {}
	} )
end

local function rarity_option()
	local scales = { 0.5, 1, 2, 3 }
	local names = { "Slow", "Normal", "Generous", "High" }
	local function index()
		local value = Engine.GetDvarFloat( "cg_lootRarityScale" )
		for i, scale in ipairs( scales ) do if value <= scale then return i end end
		return #scales
	end
	local function change( direction )
		Engine.SetDvarFloat( "cg_lootRarityScale", scales[(index() - 1 + direction) % #scales + 1] )
	end
	return {
		buttonType = "GenericButtonScrollable",
		buttonText = "Supply Drop Rarity",
		buttonDesc = "Adjust the chance of Legendary, Epic and Heroic rewards. Guaranteed rarity and eligible items stay the same.",
		buttonDisplayFunc = function () return names[index()] end,
		buttonLeftFunc = function () change( -1 ) end,
		buttonRightFunc = function () change( 1 ) end
	}
end

local function get_rank_mode( is_zombies )
	if is_zombies then
		return {
			play_mode = CoD.PlayMode.Zombies,
			stats_group = CoD.StatsGroup.Coop,
			prestige_key = "prestigeLevel",
			xp_key = "totalXP",
			rank_file = RankTable_ZM.File
		}
	end

	return {
		play_mode = CoD.PlayMode.Core,
		stats_group = CoD.StatsGroup.Ranked,
		prestige_key = "prestige",
		xp_key = "experience",
		rank_file = RankTable.File
	}
end

local function get_prestige( mode, controller )
	return tonumber( Engine.GetPlayerData( controller, mode.stats_group, mode.prestige_key ) ) or 0
end

local function get_max_prestige( mode )
	return tonumber( Rank.GetMasterPrestigeLevel( mode.play_mode ) ) or 0
end

local function get_max_rank( mode, prestige )
	return tonumber( Rank.GetMaxRank( prestige, mode.play_mode ) ) or 0
end

local function get_effective_xp( mode, controller )
	return tonumber( Engine.GetPlayerDataMPXP( controller, mode.stats_group ) ) or 0
end

local function get_rank( mode, controller )
	local prestige = get_prestige( mode, controller )
	local rank = tonumber( Lobby.GetRankForXP( get_effective_xp( mode, controller ), prestige, mode.rank_file ) ) or 0
	return math.max( 0, math.min( rank, get_max_rank( mode, prestige ) ) )
end

local function get_rank_display( mode, rank )
	local display = Rank.GetRankDisplay( rank, mode.play_mode )
	if display == nil or display == "" then
		return tostring( rank + 1 )
	end

	return tostring( display )
end

local function write_rank_stat( mode, controller, key, value )
	value = math.floor( value )
	if mode.stats_group == CoD.StatsGroup.Ranked then
		Engine.ExecNow( "setPlayerDataInt " .. key .. " " .. value )
	else
		Engine.SetPlayerData( controller, mode.stats_group, key, value )
	end
	Engine.ExecNow( "uploadStats", controller )
end

local function set_rank( mode, controller, rank )
	rank = math.max( 0, math.min( rank, get_max_rank( mode, get_prestige( mode, controller ) ) ) )
	local raw_xp = tonumber( Engine.GetPlayerData( controller, mode.stats_group, mode.xp_key ) ) or 0
	local redeemed_xp = get_effective_xp( mode, controller ) - raw_xp
	local min_xp = tonumber( Rank.GetRankMinXP( rank, mode.play_mode ) ) or 0
	write_rank_stat( mode, controller, mode.xp_key, math.max( 0, min_xp - redeemed_xp ) )
end

local function set_prestige( mode, controller, prestige )
	local rank = get_rank( mode, controller )
	prestige = math.max( 0, math.min( prestige, get_max_prestige( mode ) ) )
	write_rank_stat( mode, controller, mode.prestige_key, prestige )
	set_rank( mode, controller, rank )
end

local function find_rank_for_display( mode, prestige, text )
	local max_rank = get_max_rank( mode, prestige )
	for rank = 0, max_rank do
		if get_rank_display( mode, rank ) == text then
			return rank
		end
	end

	local level = tonumber( text )
	if level then
		return math.max( 0, math.min( level - 1, max_rank ) )
	end
end

local function refresh_button( element )
	element:processEvent( {
		name = "content_refresh"
	} )
end

local function is_arrow_hovered( element )
	return element.ArrowLeft and element.ArrowLeft.m_mouseOver or
		element.ArrowRight and element.ArrowRight.m_mouseOver
end

local function open_number_keyboard( element, event, controller, title )
	if is_arrow_hovered( element ) then
		return element:dispatchEventToChildren( event )
	end

	element.keyboardOpen = true
	Engine.OpenScreenKeyboard( controller, title, "", 4,
		LUI.VerificationLevel.NoInvalidNoSpacesNotEmptyOnlyNumbers, false, CoD.KeyboardInputTypes.Normal )
end

local function keyboard_handlers( on_text )
	return {
		text_input_complete = function ( element, event )
			if not element.keyboardOpen then
				return
			end

			element.keyboardOpen = false
			if event.text and event.text ~= "" then
				on_text( event.text )
				refresh_button( element )
			end
		end,
		text_input_canceled = function ( element )
			element.keyboardOpen = false
		end
	}
end

local function rank_options( controller, is_zombies )
	local mode = get_rank_mode( is_zombies )

	local function step_prestige( delta )
		return function ()
			local count = get_max_prestige( mode ) + 1
			set_prestige( mode, controller, ( get_prestige( mode, controller ) + delta ) % count )
		end
	end

	local function step_rank( delta )
		return function ()
			local count = get_max_rank( mode, get_prestige( mode, controller ) ) + 1
			set_rank( mode, controller, ( get_rank( mode, controller ) + delta ) % count )
		end
	end

	return {
		{
			buttonType = "GenericButtonScrollable",
			buttonText = Engine.Localize( "Prestige" ),
			buttonDesc = Engine.Localize( "Edit prestige. Press select to enter a value." ),
			buttonDisplayFunc = function ()
				return tostring( get_prestige( mode, controller ) )
			end,
			buttonLeftFunc = step_prestige( -1 ),
			buttonRightFunc = step_prestige( 1 ),
			buttonActionFunc = function ( element, event )
				return open_number_keyboard( element, event, controller, Engine.Localize( "Prestige" ) )
			end,
			handlers = keyboard_handlers( function ( text )
				local prestige = tonumber( text )
				if prestige then
					set_prestige( mode, controller, prestige )
				end
			end )
		},
		{
			buttonType = "GenericButtonScrollable",
			buttonText = Engine.Localize( "Rank" ),
			buttonDesc = Engine.Localize( "Edit rank. Press select to enter a value." ),
			buttonDisplayFunc = function ()
				return get_rank_display( mode, get_rank( mode, controller ) )
			end,
			buttonLeftFunc = step_rank( -1 ),
			buttonRightFunc = step_rank( 1 ),
			buttonActionFunc = function ( element, event )
				return open_number_keyboard( element, event, controller, Engine.Localize( "Rank" ) )
			end,
			handlers = keyboard_handlers( function ( text )
				local rank = find_rank_for_display( mode, get_prestige( mode, controller ), text )
				if rank then
					set_rank( mode, controller, rank )
				end
			end )
		}
	}
end

local function append_options( options, extra )
	for _, option in ipairs( extra ) do
		table.insert( options, option )
	end

	return options
end

local function multiplayer_options( controller )
	local items_toggle = toggle_dvar( "cg_unlockall_items" )
	local loot_toggle = toggle_dvar( "cg_unlockall_loot" )

	return append_options( {
		{
			buttonType = "GenericButton",
			buttonText = Engine.Localize( "Unlock Multiplayer Progression" ),
			buttonDesc = Engine.Localize(
				"Permanently unlock Multiplayer progression, stats, and challenges." ),
			buttonActionFunc = function ( element )
				open_unlock_confirmation( element, controller, "unlockstatsmp",
					"WARNING: This permanently changes Multiplayer progression and stats. " ..
					"It cannot automatically be undone." )
			end
		},
		{
			buttonType = "GenericButtonScrollable",
			buttonText = Engine.Localize( "Unlock All Items" ),
			buttonDesc = Engine.Localize( "Override normal item availability." ),
			buttonDisplayFunc = get_toggle_text( "cg_unlockall_items" ),
			buttonLeftFunc = items_toggle,
			buttonRightFunc = items_toggle
		},
		{
			buttonType = "GenericButtonScrollable",
			buttonText = Engine.Localize( "Unlock All Loot" ),
			buttonDesc = Engine.Localize( "Override loot item availability." ),
			buttonDisplayFunc = get_toggle_text( "cg_unlockall_loot" ),
			buttonLeftFunc = loot_toggle,
			buttonRightFunc = loot_toggle
		}
	}, rank_options( controller, false ) )
end

local function zombies_options( controller )
	local loot_toggle = toggle_dvar( "cg_unlockall_loot" )
	local consumables_toggle = toggle_dvar( "cg_unlimited_zm_consumables" )

	return append_options( {
		{
			buttonType = "GenericButton",
			buttonText = Engine.Localize( "Unlock Zombies Progression" ),
			buttonDesc = Engine.Localize(
				"Permanently unlock Zombies rank and Hidden Challenges." ),
			buttonActionFunc = function ( element )
				open_unlock_confirmation( element, controller, "unlockstatszm",
					"WARNING: This permanently changes Zombies progression, including rank " ..
					"and Hidden Challenges. It cannot automatically be undone." )
			end
		},
		{
			buttonType = "GenericButton",
			buttonText = Engine.Localize( "Unlock Zombies Easter Eggs" ),
			buttonDesc = Engine.Localize(
				"Permanently unlock supported quest records, Groesten Haus, Tortured Path chapters and survival maps, " ..
				"including Sword of Barbarossa access. Red Talon still requires its per-match puzzle." ),
			buttonActionFunc = function ( element )
				open_unlock_confirmation( element, controller, "unlockzmeastereggs",
					"WARNING: This permanently completes supported Zombies quest and map progression. " ..
					"It does not change rank or character challenges and cannot automatically be undone." )
			end
		},
		{
			buttonType = "GenericButtonScrollable",
			buttonText = Engine.Localize( "Unlock All Loot" ),
			buttonDesc = Engine.Localize( "Override loot item availability." ),
			buttonDisplayFunc = get_toggle_text( "cg_unlockall_loot" ),
			buttonLeftFunc = loot_toggle,
			buttonRightFunc = loot_toggle
		},
		{
			buttonType = "GenericButtonScrollable",
			buttonText = Engine.Localize( "Unlimited Zombies Consumables" ),
			buttonDesc = Engine.Localize( "Override Zombies consumable quantities." ),
			buttonDisplayFunc = get_toggle_text( "cg_unlimited_zm_consumables" ),
			buttonLeftFunc = consumables_toggle,
			buttonRightFunc = consumables_toggle
		}
	}, rank_options( controller, true ) )
end

local function build_unlocks_menu( menu_name, properties, options_factory )
	local self = LUI.UIElement.new( {
		left = 0,
		right = 0,
		top = 0,
		bottom = 0,
		leftAnchor = true,
		rightAnchor = true,
		topAnchor = true,
		bottomAnchor = true
	} )
	self.id = menu_name
	self:playSound( "menu_open" )

	properties = properties or {}
	local controller = get_controller_index( self, properties )
	local scoped_data = LUI.FlowManager.GetScopedData( self )
	scoped_data.gridData = options_factory( controller )
	table.insert( scoped_data.gridData, rarity_option() )

	local background = LUI.MenuBuilder.BuildRegisteredType( "GenericMenuBackground", {
		controllerIndex = controller,
		fontIconSet = properties.fontIconSet
	} )
	background.id = "S2xUnlocksBackground"
	background:setAnchors( 0, 0, 0, 0, 0 )
	background:setBottom( 0, 0 )
	background:setLeft( 0, 0 )
	background:setRight( 0, 0 )
	background:setTop( 0, 0 )
	self:addElement( background )

	local helper_bar = LUI.MenuBuilder.BuildRegisteredType( "button_helper_bar", {
		controllerIndex = controller,
		fontIconSet = properties.fontIconSet
	} )
	helper_bar.id = "S2xUnlocksButtonHelperBar"
	helper_bar:setAnchors( 0, 0, 1, 0, 0 )
	helper_bar:setBottom( _1080p * -55, 0 )
	helper_bar:setLeft( 0, 0 )
	helper_bar:setRight( 0, 0 )
	helper_bar:setTop( _1080p * -105, 0 )
	self:addElement( helper_bar )

	local options = LUI.MenuBuilder.BuildRegisteredType( "OptionButtonsGrid", {
		controllerIndex = controller,
		fontIconSet = properties.fontIconSet,
		OptionsGrid_maxVisibleRows = 7,
		OptionsGrid_verticalAlignment = LUI.Alignment.Top
	} )
	options.id = "S2xUnlocksOptions"
	options:setAnchors( 0, 1, 0, 1, 0 )
	options:setBottom( _1080p * 952.08, 0 )
	options:setLeft( 0, 0 )
	options:setRight( _1080p * 900, 0 )
	options:setTop( _1080p * 200, 0 )
	self:addElement( options )

	local title = LUI.MenuBuilder.BuildRegisteredType( "GenericMenuTitle", {
		controllerIndex = controller,
		fontIconSet = properties.fontIconSet
	} )
	title.id = "S2xUnlocksTitle"
	title:setAnchors( 0, 0, 0, 1, 0 )
	title:setBottom( _1080p * 173, 0 )
	title:setLeft( _1080p * 100, 0 )
	title:setRight( _1080p * -100, 0 )
	title:setTop( _1080p * 125, 0 )
	if title.Title then
		title.Title:setFont( FONTS.BodyBoldFont.Font )
		title.Title:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
		title.Title:setText( Engine.Localize( "UNLOCKS" ), 0 )
	end
	if title.zm_title_divider0 then
		title.zm_title_divider0:setRight( _1080p * 1727, 0 )
	end
	self:addElement( title )

	local description = LUI.MenuBuilder.BuildRegisteredType( "GenericMenuDescription", {
		controllerIndex = controller,
		fontIconSet = properties.fontIconSet
	} )
	description.id = "S2xUnlocksDescription"
	description:setAnchors( 0, 1, 0, 1, 0 )
	description:setBottom( _1080p * 303, 0 )
	description:setLeft( _1080p * 924, 0 )
	description:setRight( _1080p * 1820, 0 )
	description:setTop( _1080p * 200, 0 )
	if description.DescriptionText then
		description.DescriptionText:setText( "", 0 )
	end
	if description.DescriptionTitle then
		description.DescriptionTitle:setText( "", 0 )
	end
	self:addElement( description )

	local helper = helper_bar:BeginSet()
	helper = helper:AddBackButton()
	helper = helper:AddLeft( LuaButton.primary, "LUA_MENU_SELECT", nil )
	helper:Finish()

	if title.SetTitle then
		title:SetTitle( "UNLOCKS", "LUA_MENU_SOLDIER" )
	end

	return self
end

LUI.MenuBuilder.registerType( "s2x_unlocks_mp_menu", function ( menu, properties )
	return build_unlocks_menu( "s2x_unlocks_mp_menu", properties, multiplayer_options )
end )

LUI.MenuBuilder.registerType( "s2x_unlocks_zm_menu", function ( menu, properties )
	return build_unlocks_menu( "s2x_unlocks_zm_menu", properties, zombies_options )
end )

local menu_builders = LUI.MenuBuilder.m_types_build or m_types_build
assert( type( menu_builders ) == "table", "Missing LUI menu builder registry" )

local function patch_soldier_menu( soldier_menu, properties, requested_menu_name )
	if not soldier_menu then
		return soldier_menu
	end

	local menu_name = requested_menu_name or soldier_menu.id
	if menu_name ~= "soldierscreen_menu" and menu_name ~= "zm_soldier_menu" then
		return soldier_menu
	end

	if soldier_menu.S2xUnlocksTab then
		return soldier_menu
	end

	properties = properties or {}
	local controller = get_controller_index( soldier_menu, properties )
	local is_zombies = menu_name == "zm_soldier_menu"
	local left = 100
	local right = is_zombies and 600 or 440
	local top = is_zombies and 593 or 386
	local bottom = top + 62
	local unlocks_tab = LUI.MenuBuilder.BuildRegisteredType( "soldierscreen_tab_button", {
		controllerIndex = controller,
		fontIconSet = properties.fontIconSet
	} )
	unlocks_tab.id = "S2xUnlocksTab"
	unlocks_tab:setAnchors( 0, 1, 0, 1, 0 )
	unlocks_tab:setBottom( _1080p * bottom, 0 )
	unlocks_tab:setLeft( _1080p * left, 0 )
	unlocks_tab:setRight( _1080p * right, 0 )
	unlocks_tab:setTop( _1080p * top, 0 )
	if unlocks_tab.Icon then
		unlocks_tab.Icon:setImage( RegisterMaterial( "menu_soldier_dossier" ), 0 )
	end
	if unlocks_tab.Name then
		unlocks_tab.Name:setText( Engine.Localize( "UNLOCKS" ), 0 )
	end
	soldier_menu:addElement( unlocks_tab )
	soldier_menu.S2xUnlocksTab = unlocks_tab

	if not is_zombies and soldier_menu.ActiveBoostTab then
		soldier_menu.ActiveBoostTab:setBottom( _1080p * 510, 0 )
		soldier_menu.ActiveBoostTab:setTop( _1080p * 448, 0 )
	end
	if not is_zombies and soldier_menu.ActiveXPBoosts then
		soldier_menu.ActiveXPBoosts:setBottom( _1080p * 992.83, 0 )
		soldier_menu.ActiveXPBoosts:setTop( _1080p * 541.92, 0 )
	end

	local function open_unlocks_menu( event )
		local unlocks_menu = is_zombies and "s2x_unlocks_zm_menu" or "s2x_unlocks_mp_menu"
		ACTIONS.OpenMenu( unlocks_menu, true, event.controller or controller )
		local scoped_data = LUI.FlowManager.GetScopedData( soldier_menu )
		if scoped_data then
			scoped_data.subMenu = true
		end
	end

	unlocks_tab:addEventHandler( "button_action", function ( element, event )
		open_unlocks_menu( event )
	end )
	unlocks_tab:addEventHandler( "gamepad_button", function ( element, event )
		if CONDITIONS.ButtonRight( soldier_menu, event ) and
			CONDITIONS.IsInFocus( element ) and CONDITIONS.IsButtonDown( soldier_menu, event ) then
			open_unlocks_menu( event )
			ACTIONS.PlaySelectSound()
		end
	end )

	soldier_menu:updateNavigation()
	return soldier_menu
end

local stock_soldier_builder = menu_builders["soldierscreen_menu"]
assert( type( stock_soldier_builder ) == "function", "Missing Soldier menu builder" )

menu_builders["soldierscreen_menu"] = function ( menu, properties )
	return patch_soldier_menu( stock_soldier_builder( menu, properties ), properties )
end

local stock_zombies_soldier_builder = menu_builders["zm_soldier_menu"]
if type( stock_zombies_soldier_builder ) == "function" then
	menu_builders["zm_soldier_menu"] = function ( menu, properties )
		return patch_soldier_menu( stock_zombies_soldier_builder( menu, properties ), properties,
			"zm_soldier_menu" )
	end
end

local stock_build_registered_type = LUI.MenuBuilder.BuildRegisteredType
LUI.MenuBuilder.BuildRegisteredType = function ( menu_name, properties )
	local built_menu = stock_build_registered_type( menu_name, properties )
	if menu_name == "soldierscreen_menu" or menu_name == "zm_soldier_menu" then
		return patch_soldier_menu( built_menu, properties, menu_name )
	end

	return built_menu
end

local stock_change_page = TabMenuBase.ChangePage
TabMenuBase.ChangePage = function ( tab_menu, controller, previous_page, previous_index, next_page,
	next_index, properties_func )
	stock_change_page( tab_menu, controller, previous_page, previous_index, next_page, next_index,
		properties_func )
	local requested_menu_name = next_page and next_page.GetMenuName and
		next_page.GetMenuName( controller ) or nil
	patch_soldier_menu( tab_menu.CurrentMenuPage, {
		controllerIndex = controller
	}, requested_menu_name )
end

local root = Engine.GetLuiRoot()
local menu_info = root and root.flowManager and
	LUI.FlowManager.GetTopMenuInfo( root.flowManager.menuInfoStack ) or nil
local current_page = menu_info and menu_info.menu and menu_info.menu.CurrentMenuPage or nil
patch_soldier_menu( current_page, {
	controllerIndex = Engine.GetFirstActiveController()
} )
