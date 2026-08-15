#pragma once

namespace unlock_zombies
{
	struct challenge_unlock_result
	{
		bool enabled{};
		int completed{};
		int total{};
	};

	challenge_unlock_result enable_challenges();
	bool enable_consumables();
}
