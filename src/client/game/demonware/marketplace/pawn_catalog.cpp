#include <std_include.hpp>
#include "pawn_catalog.hpp"

namespace demonware::pawn_catalog
{
	namespace
	{
		std::atomic<std::shared_ptr<const catalog>> current;
	}

	std::shared_ptr<const catalog> get_snapshot()
	{
		auto result = current.load();
		return result && result->source && result->source == loot_catalog::get_snapshot() ? result : nullptr;
	}

	void publish(catalog value)
	{
		current.store(std::make_shared<const catalog>(std::move(value)));
	}
}
