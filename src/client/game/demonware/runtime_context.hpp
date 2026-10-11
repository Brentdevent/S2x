#pragma once

namespace demonware::runtime_context
{
	struct zombie_rank
	{
		int prestige{};
		int experience{};

		bool operator==(const zombie_rank&) const = default;
	};

	struct identity
	{
		std::uint64_t user_id{};
		std::string persona_name{};
		std::uint32_t producer_thread_id{};
		float loot_rarity_scale{1.0f};
		std::optional<zombie_rank> zombies{};
	};

	std::shared_ptr<const identity> get_snapshot();
	std::uint64_t get_local_user_id();
	bool publish(identity value);
	void clear();
}
