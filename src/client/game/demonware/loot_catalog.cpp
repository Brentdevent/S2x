#include <std_include.hpp>

#include "loot_catalog.hpp"
#include "loot_catalog_lifecycle.hpp"

namespace demonware::runtime
{
	loot_catalog_lifecycle& loot_lifecycle()
	{
		static loot_catalog_lifecycle value{};
		return value;
	}
}

namespace demonware::loot_catalog
{
	std::optional<supply_drop> find_supply_drop(const catalog& source, const std::string_view backend_id)
	{
		if (backend_id.empty() || backend_id.size() > 256)
		{
			return std::nullopt;
		}

		std::optional<supply_drop> result{};
		for (const auto& drop : source.supply_drops)
		{
			if (drop.backend_id != backend_id)
			{
				continue;
			}

			if (result && *result != drop)
			{
				return std::nullopt;
			}

			result = drop;
		}

		return result;
	}

	std::shared_ptr<const catalog> get_snapshot()
	{
		return runtime::loot_lifecycle().snapshot();
	}
}
