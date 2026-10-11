if game:issingleplayer() or Engine.IsZombiesMode() then
	return
end

local stock_expired_challenge = DwDataUtils.GetPlayerExpiredChallenge
DwDataUtils.GetPlayerExpiredChallenge = function ( controller, id )
	local challenge = stock_expired_challenge( controller, id )
	if not challenge then
		return nil
	end
	-- The stock inventory screen reuses a scheduled offer for an expired card.
	-- A consumed offer's Completed status would play the redemption animation.
	-- Project the stock Expired UI status without changing the scheduled cache.
	local expired = {}
	for key, value in pairs( challenge ) do
		expired[key] = value
	end
	expired.status = GameChallengeStatus.GameChallengeStatus_Expired
	return expired
end
