if game:issingleplayer() or Engine.InFrontend() then
	return
end

local builders = LUI.MenuBuilder.m_types_build
local stock_widget = builders.PeriodicChallengeProgressWidget
builders.PeriodicChallengeProgressWidget = function ( menu, properties )
	local widget = stock_widget( menu, properties )
	local controller = properties and properties.controllerIndex or widget:getRootController()
	local stock_event = widget.m_eventHandlers.achievementEngine
	if not stock_event then
		return widget
	end

	-- The stock builder captures its UC actions in these animation callbacks.
	-- Wrap the callbacks on this widget, keeping its layout and display logic.
	local showing = widget.ChallengeDesc.sequences.NotificationShowingFast[1]
	local animate_out = widget.ChallengeDesc.sequences.AnimateOut[1]
	local stock_display_unlock = showing[#showing]
	local stock_animate_out = animate_out[#animate_out]

	local function drain_queue()
		widget._challengeLock = false
		widget._currentEventIndex = 1
		ACTIONS.AnimateSequence( widget, "HideImmediate" )
		if not Engine.AE_ArePlayerAchievementsFetched( controller ) then
			-- A full fetch clears the native cache. Keep the pending events until
			-- PlayerAchievementsFetched, but let the current popup disappear.
			return
		end

		while #widget._challengeArray > 0 and not widget._challengeLock do
			-- Missing/expired targets can make the stock action return without
			-- displaying anything. Continue to the next event in that case.
			stock_animate_out()
		end
	end

	showing[#showing] = function ()
		-- The stock action consumes a same-Order update even if its display
		-- helper rejects it. Schedule the normal fade-out first so that early
		-- return cannot strand the popup. Valid updates replace this sequence.
		ACTIONS.AnimateSequence( widget, "NotificationShowing" )
		if Engine.AE_ArePlayerAchievementsFetched( controller ) then
			stock_display_unlock()
		end
		return nil
	end

	animate_out[#animate_out] = function ()
		drain_queue()
		return nil
	end

	widget:registerEventHandler( "achievementEngine", function ( element, event )
		local notification = event.eventType == AEEventType.ProgressionUpdate
			or event.eventType == AEEventType.AchievementClaimable
		if notification and event.kind ~= GameChallengeType.GameAchievementKind_AlwaysOn
			and event.kind ~= GameChallengeType.GameAchievementKind_Community then
			if not Engine.AE_ArePlayerAchievementsFetched( controller )
				or ( not widget._challengeLock and #widget._challengeArray > 0 ) then
				table.insert( widget._challengeArray, event )
				if not widget._challengeLock then
					drain_queue()
				end
				return
			end
		end

		stock_event( element, event )
		if event.eventType == AEEventType.PlayerAchievementsFetched and event.success
			and not widget._challengeLock and #widget._challengeArray > 0 then
			drain_queue()
		end
	end )
	return widget
end
