#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace demonware::runtime_context
{
	struct identity
	{
		std::uint64_t user_id{};
		std::string persona_name{};
		std::uint32_t producer_thread_id{};
		float loot_rarity_scale{1.0f};
	};

	// Readers retain one immutable owned generation. A normal client without an
	// identity must fail closed; zero is never synthesized as a local player ID.
	std::shared_ptr<const identity> get_snapshot();
	bool publish(identity value);
	void clear();
}
