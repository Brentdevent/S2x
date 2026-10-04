if game:issingleplayer() or not Engine.InFrontend() or not Engine.IsZombiesMode() then
	return
end

local function recover_reward( controller, achievement_id, event_group, reward_reference )
	local achievement = Engine.AE_GetPlayerAchievementInfo( controller, achievement_id )
	local owned = true
	if reward_reference then
		local guid = Engine.GetItemGUIDFromReference( reward_reference )
		owned = ( tonumber( Engine.Inventory_GetItemQuantity( controller, guid, false ) ) or 0 ) > 0
	end

	if not achievement or achievement.fullfilledTimes == 0 or not owned then
		AchievementEngineUtils.AE_SendComplexGameEvent( controller, AEComplexEvents.ZombiesSuperEE,
			{ event_group, 0 } )
	end
end

local builders = LUI.MenuBuilder.m_types_build
local stock_hub = builders.hub_menu
builders.hub_menu = function ( menu, properties )
	local hub = stock_hub( menu, properties )
	if not CONDITIONS.IsOnlineMatch() or not CONDITIONS.IsMTX9Enabled() then
		return hub
	end

	local controller = properties and properties.controllerIndex or
		LUI.FlowManager.GetScopedData( hub ).exclusiveControllerIndex
	if not controller or Engine.GetPlayerData( controller, CoD.StatsGroup.Coop, "prestigeLevel" ) ~= 10 then
		return hub
	end

	-- Stock hub recovery stops after MTX9Backtrace is set, even on profiles from
	-- builds without these rewards. Recheck only missing rewards at hub entry;
	-- the existing event settlement owns persistence, retries and cache refresh.
	local rank = ZMCacUtils.ComputeRank( controller )
	local rewards = ZMUtils.MasterPrestigeReward
	if rank >= rewards.rewardRank1 then
		recover_reward( controller, 1142, 30, "weaponcharm_mtx8_01" )
	end

	if rank >= rewards.rewardRank2 then
		recover_reward( controller, 1144, 32, "zom_stun_01" )
	end

	if rank >= rewards.rewardRank3 and
		Rank.GetRankMaxXP( rank ) <= AAR.GetCareerExperience( controller, CoD.PlayMode.Zombies ) then
		recover_reward( controller, 1145, 33 )
	end

	return hub
end
