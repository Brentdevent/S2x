local FIND_MATCH_COLUMN_COUNT = 4
local FIND_MATCH_LAYOUT_NONE = 0

local STOCK_BUTTON_TEXT = {
	PublicMatch = "LUA_MENU_PUBLIC_MATCH_CAPS",
	CustomMatch = "LUA_MENU_CUSTOM_MATCH_CAPS",
	ReplayPrologue = "LUA_MENU_REPLAY_TRAINING_CAPS",
	Campaign = "LUA_MENU_CAMPAIGN_CAPS",
	Multiplayer = "MENU_MULTIPLAYER_CAPS",
	Zombies = "LUA_MENU_ZOMBIES_CAPS"
}

local function get_find_match_state( menu )
	if not menu then
		return nil
	end

	local scoped_data = LUI.FlowManager.GetScopedData( menu )
	return scoped_data and scoped_data.findGameMenu or nil
end

local function find_stock_button( find_match_state, button_text )
	for _, button_list in ipairs( {
		find_match_state.topButtons,
		find_match_state.bottomButtons
	} ) do
		for _, button in ipairs( button_list or {} ) do
			if button.text == button_text then
				return button
			end
		end
	end

	return nil
end

local function get_controller_index( controller, fallback_controller )
	if type( controller ) == "number" then
		return controller
	end

	if type( controller ) == "table" and type( controller.controller ) == "number" then
		return controller.controller
	end

	return fallback_controller
end

local function open_server_browser( controller, fallback_controller )
	if not Engine.AllSplitscreenPlayersInParty() then
		return
	end

	local controller_index = get_controller_index( controller, fallback_controller )
	if type( controller_index ) ~= "number" then
		return
	end

	AAR.ClearAAR()
	ACTIONS.OpenMenu( "s2x_server_browser", false, controller_index )
end

local function create_server_browser_button( template, controller, labels )
	return {
		image = template.image,
		blurImage = template.blurImage,
		text = labels.SERVER_BROWSER,
		desc = labels.SERVER_BROWSER_FIND_MATCH_DESCRIPTION,
		actionFunc = function ( _, event )
			open_server_browser( event, controller )
		end,
		disabledFunc = template.disabledFunc
	}
end

local function set_find_match_data_sources( controller )
	local find_match_data = DataSources and DataSources.inFrontend and
		DataSources.inFrontend.MP and DataSources.inFrontend.MP.FindMatch or nil
	if not find_match_data then
		return
	end

	if find_match_data.numColumns then
		find_match_data.numColumns:SetValue( controller, FIND_MATCH_COLUMN_COUNT )
	end
	if find_match_data.currentFirstColumn then
		find_match_data.currentFirstColumn:SetValue( controller, 0 )
	end
	if find_match_data.maxFirstColumn then
		find_match_data.maxFirstColumn:SetValue( controller, 0 )
	end
end

local function configure_multiplayer_buttons( menu, controller, labels )
	local find_match_state = get_find_match_state( menu )
	if not find_match_state then
		return
	end

	local public_match = find_stock_button( find_match_state, STOCK_BUTTON_TEXT.PublicMatch )
	local custom_match = find_stock_button( find_match_state, STOCK_BUTTON_TEXT.CustomMatch )
	local campaign = find_stock_button( find_match_state, STOCK_BUTTON_TEXT.Campaign )
	local zombies = find_stock_button( find_match_state, STOCK_BUTTON_TEXT.Zombies )
	if not public_match or not custom_match or not campaign or not zombies then
		return
	end

	local server_browser = create_server_browser_button(
		public_match,
		controller,
		labels
	)

	find_match_state.layoutStyle = FIND_MATCH_LAYOUT_NONE
	find_match_state.topButtons = {
		server_browser,
		custom_match,
		campaign,
		zombies
	}
	find_match_state.bottomButtons = {}
	find_match_state.numTotalColumns = FIND_MATCH_COLUMN_COUNT
	find_match_state.numColWindows = FIND_MATCH_COLUMN_COUNT
	find_match_state.controllerIndex = controller

	set_find_match_data_sources( controller )
end

local function configure_zombies_buttons( menu, controller, labels )
	local find_match_state = get_find_match_state( menu )
	if not find_match_state then
		return
	end

	-- Public Match is present in every stock Zombies layout and supplies the
	-- native Zombies tile art and availability rules.
	local public_match = find_stock_button( find_match_state, STOCK_BUTTON_TEXT.PublicMatch )
	local custom_match = find_stock_button( find_match_state, STOCK_BUTTON_TEXT.CustomMatch )
	local replay_prologue = find_stock_button( find_match_state, STOCK_BUTTON_TEXT.ReplayPrologue )
	local campaign = find_stock_button( find_match_state, STOCK_BUTTON_TEXT.Campaign )
	local multiplayer = find_stock_button( find_match_state, STOCK_BUTTON_TEXT.Multiplayer )
	if not public_match or not custom_match or not replay_prologue or
		not campaign or not multiplayer then
		return
	end

	find_match_state.layoutStyle = FIND_MATCH_LAYOUT_NONE
	find_match_state.topButtons = {
		create_server_browser_button( public_match, controller, labels ),
		custom_match,
		replay_prologue
	}
	find_match_state.bottomButtons = {
		campaign,
		multiplayer
	}
	find_match_state.numTotalColumns = FIND_MATCH_COLUMN_COUNT
	find_match_state.numColWindows = FIND_MATCH_COLUMN_COUNT
	find_match_state.controllerIndex = controller

	set_find_match_data_sources( controller )
end

local function apply_find_match_labels( menu, labels )
	if menu.Menutitle and menu.Menutitle.Title then
		menu.Menutitle.Title:setText( Engine.Localize( labels.FIND_MATCH_TITLE ), 0 )
	end
	if menu.JoinLobbySubText then
		menu.JoinLobbySubText:setText( Engine.Localize( labels.FIND_MATCH_DESCRIPTION ), 0 )
	end
end

local function configure_server_browser_shortcut( menu, controller, labels )
	if CONDITIONS.IsPreLaunchDemo() then
		return
	end

	local helper_bar = menu.ButtonHelperBar
	if not helper_bar or not helper_bar.DoesHaveButton or
		not helper_bar:DoesHaveButton( LuaButton.left_trigger ) then
		return
	end

	if helper_bar.SetButtonText then
		helper_bar:SetButtonText(
			LuaButton.left_trigger,
			labels.SERVER_BROWSER_HELPER,
			true
		)
	end
	if helper_bar.ForceSetButtonCallback then
		helper_bar:ForceSetButtonCallback( LuaButton.left_trigger, function ( _, callback_controller )
			open_server_browser( callback_controller, controller )
		end )
	end
end

local function wrap_stock_controller( stock_controller, labels )
	assert( type( stock_controller ) == "table", "Missing stock findgame_menu_uc table" )
	assert( type( labels ) == "table", "Missing S2x Find Match labels" )

	if stock_controller.s2xWrapped then
		return stock_controller
	end

	local stock_preload = assert( stock_controller.PreLoadFunc )
	local stock_postload = assert( stock_controller.PostLoadFunc )
	local stock_default_focus = assert( stock_controller.FUNCTOR_GetDefaultFocusGrid )

	stock_controller.PreLoadFunc = function ( menu, controller, properties )
		local preload_result = stock_preload( menu, controller, properties )
		if Engine.IsZombiesMode() then
			configure_zombies_buttons( menu, controller, labels )
		else
			configure_multiplayer_buttons( menu, controller, labels )
		end
		return preload_result
	end

	stock_controller.PostLoadFunc = function ( menu, controller, properties )
		local postload_result = stock_postload( menu, controller, properties )
		if Engine.IsZombiesMode() then
			configure_server_browser_shortcut( menu, controller, labels )
			return postload_result
		end

		apply_find_match_labels( menu, labels )
		configure_server_browser_shortcut( menu, controller, labels )
		return postload_result
	end

	-- The four-column S2x grid needs to clamp positions saved by the wider stock
	-- grid. Keep Zombies on the original focus path and do not mutate hub state.
	stock_controller.FUNCTOR_GetDefaultFocusGrid = function ( menu )
		local focus_position = stock_default_focus( menu )
		if Engine.IsZombiesMode() then
			return focus_position
		end

		return {
			x = math.min(
				focus_position and focus_position.x or 0,
				FIND_MATCH_COLUMN_COUNT - 1
			),
			y = 0
		}
	end

	stock_controller.s2xWrapped = true
	return stock_controller
end

return {
	wrap_stock_controller = wrap_stock_controller
}
