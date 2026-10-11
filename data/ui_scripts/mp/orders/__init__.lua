if game:issingleplayer() or not Engine.InFrontend() then
	return
end

-- Local CP rewards use the normal currency descriptor; stock Orders only
-- format XP, Armory Credits and Social Score.
local stock_reward = AchievementEngineUtils.GetRewardAndIcon
AchievementEngineUtils.GetRewardAndIcon = function ( reward, challenge_id )
	if reward and reward.currencyID == QuarterMasterUtils.InventoryCurrencyType.CoDPoints
		and reward.currencyAmount and reward.currencyAmount > 0 then
		return reward.currencyAmount .. " CP", "cod_points", OrderRewardLayout.Default
	end
	return stock_reward( reward, challenge_id )
end

local builders = LUI.MenuBuilder.m_types_build
local stock_button = builders.daily_orders_button
builders.daily_orders_button = function ( ... )
	local button = stock_button( ... )
	local completed = button._sequences.Completed
	button._sequences.Completed = function ()
		-- A failed claim restores Claimable, but the stock Special Order button's
		-- Completed sequence leaves the previous Loading animation running.
		button._sequences.NotLoading()
		completed()
	end
	return button
end

local stock_inventory_button = builders.periodicChallengeInventoryButton
builders.periodicChallengeInventoryButton = function ( ... )
	local button = stock_inventory_button( ... )
	local empty = button._sequences.EmptyData
	button._sequences.EmptyData = function ()
		-- A claimed slot can clear before its Redeem animation runs.
		button.LoadingIcon:AnimateSequence( "Redeem" )
		empty()
	end
	return button
end

if not Engine.IsZombiesMode() then
	return
end

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
