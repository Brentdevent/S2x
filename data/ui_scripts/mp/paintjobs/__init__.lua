if game:issingleplayer() or not UGCUtils then
	return
end

local paintshop = CoD.UGCCategory.Paintshop
local personal_menu = "manage_weapon_paintjobs_menu"
local loading_menu = "s2x_paintjobs_loading"
local stock_open_menu = UGCUtils.OpenMenuWhenPaintjobsReady

local function start_loading( popup, event )
	local controller = event.controller
	local owner = Engine.GetXUIDByController( controller )
	local in_frontend = Engine.InFrontend()
	local is_zombies = Engine.IsZombiesMode()
	local elapsed = 0
	local finished = false
	local timer = LUI.UITimer.new( 250, {
		name = "s2x_check_paintjobs",
		dispatchChildren = false
	}, nil, false, false, false, false )

	local function stop_loading()
		if finished then
			return
		end
		finished = true
		timer:Stop()
		timer:close()
	end

	popup.alwaysSendMenuClose = true
	popup:addEventHandler( "menu_close", stop_loading )
	timer.timerEventTarget = timer
	timer:registerEventHandler( "s2x_check_paintjobs", function ( element, tick )
		if finished then
			return
		end
		if Engine.GetXUIDByController( controller ) ~= owner
			or Engine.InFrontend() ~= in_frontend or Engine.IsZombiesMode() ~= is_zombies then
			stop_loading()
			LUI.FlowManager.RequestLeaveMenu( popup )
			return
		end

		-- The native online pump fetches the private cache.
		local ready = UGC_Editor.AreUGCItemsFetchedForController( controller, paintshop )
		elapsed = elapsed + tick.timeElapsed
		if not ready and elapsed < 10000 then
			return
		end

		stop_loading()
		LUI.FlowManager.RequestLeaveMenu( popup )
		if ready then
			ACTIONS.OpenMenu( personal_menu, true, controller )
		else
			CoD.EmblemPopups.OpenInfoPopup( popup, controller, "MENU_NOTICE",
				UGCUtils.Strings.LoadingFailed[paintshop], nil )
		end
	end )
	popup:addElement( timer )
end

LUI.MenuBuilder.registerPopupDef( loading_menu, function ()
	return {
		type = "loading_modal",
		id = loading_menu,
		handlers = {
			menu_create = start_loading
		},
		properties = {
			text = Engine.Localize( CoD.GetLoadingStringForUGCCategory( paintshop ) )
		}
	}
end )

UGCUtils.OpenMenuWhenPaintjobsReady = function ( menu, controller, menu_name, category )
	if menu_name ~= personal_menu or category ~= paintshop then
		return stock_open_menu( menu, controller, menu_name, category )
	end
	if LUI.FlowManager.IsMenuInStack( menu:getRootParent(), loading_menu ) then
		return
	end
	if UGC_Editor.AreUGCItemsFetchedForController( controller, paintshop ) then
		ACTIONS.OpenMenu( menu_name, true, controller )
	else
		LUI.FlowManager.RequestAddMenu( menu, loading_menu, true, controller, false )
	end
end
