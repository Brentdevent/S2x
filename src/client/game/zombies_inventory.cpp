#include <std_include.hpp>

#include "zombies_inventory.hpp"

#include "game/game.hpp"

namespace game::zombies_inventory
{
	namespace
	{
		constexpr auto tutorial_unlock_reference = "Zombie_Tutorial_Level_Unlocked";
		std::atomic_uint tutorial_unlock_guid{};
	}

	bool is_progression_item(const unsigned int item_guid)
	{
		if (!environment::is_zombies())
		{
			return false;
		}

		auto guid = tutorial_unlock_guid.load();
		if (!guid)
		{
			guid = BG_GetItemGUIDFromReference(tutorial_unlock_reference);
			if (guid)
			{
				tutorial_unlock_guid = guid;
			}
		}

		return guid && item_guid == guid;
	}
}
