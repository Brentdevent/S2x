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

local function multiplayer_options( controller )
	local items_toggle = toggle_dvar( "cg_unlockall_items" )
	local loot_toggle = toggle_dvar( "cg_unlockall_loot" )

	return {
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
	}
end

local function zombies_options( controller )
	local loot_toggle = toggle_dvar( "cg_unlockall_loot" )
	local consumables_toggle = toggle_dvar( "cg_unlimited_zm_consumables" )

	return {
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
	}
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
