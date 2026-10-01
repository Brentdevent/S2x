if game:issingleplayer() or not Engine.InFrontend() or not Engine.IsZombiesMode() then
	return
end

local builders = LUI.MenuBuilder.m_types_build
local stock_description = builders.daily_orders_descriptions
builders.daily_orders_descriptions = function ( ... )
	local description = stock_description( ... )
	local update_progress = description.UpdateProgressBar
	local set_countdown = description.SetCountdownTimer
	local order_kind

	description.UpdateProgressBar = function ( self, controller, order_type, ... )
		order_kind = nil
		if order_type == AEPeriodicType.DailyZM then
			order_kind = GameChallengeType.GameAchievementKind_Order_Daily_ZM
		elseif order_type == AEPeriodicType.WeeklyZM then
			order_kind = GameChallengeType.GameAchievementKind_Order_Weekly_ZM
		end
		return update_progress( self, controller, order_type, ... )
	end

	description.SetCountdownTimer = function ( self, controller, deadline )
		-- The stock Zombies menu hard-codes 10:00 UTC and tests the MP weekly
		-- type. Use the Achievement Engine deadline, like the MP Orders menu.
		if order_kind then
			deadline = Engine.EpochTimeToGameTime( Engine.AE_GetNextPeriodStart( order_kind ) )
		end
		return set_countdown( self, controller, deadline )
	end
	return description
end
