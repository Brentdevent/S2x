local headquarters = {}

function headquarters.wrap_vendor_controller( stock_controller )
	if stock_controller.s2xHQWrapped then
		return stock_controller
	end

	local refresh_child = assert( stock_controller.FUNCTOR_MainGridRefreshChild )
	stock_controller.FUNCTOR_MainGridRefreshChild = function ( row, ... )
		refresh_child( row, ... )
		if not Engine.IsZombiesMode() and row.Button and
			row.Button.TitleLabel:getText() == Engine.ToUpperCase( Engine.Localize( "LUA_MENU_HEADQUARTERS" ) ) then
			-- The stock HQ vendor tile executes xpartyfullhub directly. Replace
			-- only its action; keep the stock art, layout and availability rules.
			row:registerEventHandler( "button_action", function ()
				local scoped = LUI.FlowManager.GetScopedData( row )
				Hub.Callback_GoToHub( row, scoped.hubVendorMenu.controllerIndex )
			end )
		end
	end

	stock_controller.s2xHQWrapped = true
	return stock_controller
end

function headquarters.install()
	if Engine.IsZombiesMode() or Hub.S2xGoToPrivateHQ then
		return
	end

	Hub.S2xGoToPrivateHQ = Hub.Callback_GoToHub
	Hub.Callback_GoToHub = function ( menu, controller, button )
		-- In a loaded HQ this button closes the overlay and returns to gameplay.
		if Engine.IsZombiesMode() or not Engine.UseAlternateHubFlow() then
			return Hub.S2xGoToPrivateHQ( menu, controller, button )
		end

		controller = controller or Engine.GetFirstActiveController()
		if controller ~= Engine.GetFirstActiveController() or
			not Engine.AllSplitscreenPlayersInParty() then
			return
		end

		AAR.ClearAAR()
		ACTIONS.OpenMenu( "s2x_hq_browser", false, controller )
	end
end

function headquarters.create_private( menu, controller, helper_bar )
	-- Keep the stock party-leader checks, leave-lobby confirmation and full-HQ
	-- transition. Cancel discovery joins before starting a different session.
	Lobby.CancelS2xConnection()
	Hub.S2xGoToPrivateHQ( menu, controller, helper_bar )
end

return headquarters
