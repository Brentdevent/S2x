#pragma once

namespace unlock_zombies
{
	struct hidden_challenge_unlock_result
	{
		bool persisted{};
		int completed{};
		int total{};
	};

	hidden_challenge_unlock_result unlock_hidden_challenges();
}
