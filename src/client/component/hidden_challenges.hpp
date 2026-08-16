#pragma once

namespace demonware::reward_game_events
{
	struct event;
}

namespace hidden_challenges
{
	void submit_reward_game_event(demonware::reward_game_events::event event);
}
