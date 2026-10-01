#include <std_include.hpp>

#include "loot_policy.hpp"
#include "loot_compatibility.hpp"
#include "marketplace_store.hpp"
#include "pawn_catalog.hpp"
#include "supply_drop_inventory.hpp"

#include <algorithm>
#include <limits>
#include <cmath>
#include <tuple>

namespace demonware::loot_policy
{
	using namespace loot_catalog;

	namespace
	{
		std::uint64_t next_selection_value(std::uint64_t& state)
		{
			state += 0x9E3779B97F4A7C15ULL;
			auto value = state;
			value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
			value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
			return value ^ (value >> 31);
		}
	}

	std::vector<loot_item> rule_candidates(const catalog& source, const supply_drop* drop)
	{
		const auto& table = source.source_table;
		std::vector<loot_item> result;
		// Explicit LOCAL MP policy, not retail pool membership. Start with the
		// named collectible groups in StatsTable across all assigned operations.
		// Unassigned, unlocalized and dedicated-acquisition records stay out.
		constexpr std::array<std::string_view, 18> groups{
			"emote", "grip", "playercard_icon", "playercard_title", "uniforms", "costume",
			"face_camo", "weapon_charm", "weapon_class_camo", "site_reticle",
			"weapon_assault", "weapon_smg", "weapon_heavy", "weapon_sniper",
			"weapon_shotgun", "weapon_pistol", "weapon_projectile", "weapon_other"
		};
		if (!table.table_asset_available || !table.table_values_available ||
			!table.table_dimensions_valid || !table.required_columns_available ||
			(drop && !loot_compatibility::is_confirmed_mp_supply_drop(*drop)))
		{
			return result;
		}

		for (const auto& row : source.items)
		{
			const auto& item = row.item;
			if (std::ranges::find(groups, item.group) == groups.end() ||
				!item.operation || !row.operation_valid || !row.loot_flag ||
				!row.rarity_eligible || row.challenge || row.entitlement || row.collection_reward)
			{
				continue;
			}
			// Ignore is also set on usable class camos/charms; it is not a loot veto.
			// Require a real localized display asset, not just a nonempty key.
			if (!row.localized_name || !row.reference_valid || !row.presentation_available ||
				!row.rarity_valid || item.rarity < 0 || item.rarity > 4 || !item.item_id ||
				item.stock_hidden_item || item.stock_internal_costume_component || item.azm_consumable)
			{
				continue;
			}
			if (std::ranges::any_of(source.supply_drops, [&](const auto& mapped)
				{ return mapped.item_id == item.item_id; }))
			{
				continue;
			}
			result.push_back(item);
		}
		// Stable GUID ordering and one entry per item: aliases must not weight it.
		std::ranges::sort(result, [](const auto& left, const auto& right)
			{ return std::tie(left.item_id, left.reference) < std::tie(right.item_id, right.reference); });
		const auto duplicates = std::unique(result.begin(), result.end(),
			[](const auto& left, const auto& right) { return left.item_id == right.item_id; });
		result.erase(duplicates, result.end());
		return result;
	}

	std::vector<loot_item> eligible_items(const catalog& source, const supply_drop& drop,
		const marketplace_store::transaction& transaction)
	{
		auto result = rule_candidates(source, &drop);
		const auto pawning = pawn_catalog::get_snapshot();
		std::erase_if(result, [&](const loot_item& item)
		{
			if (item.item_id > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
			{
				return true;
			}
			// Common/rare MP slots have no duplicate protection. Keep owned
			// cosmetics eligible when the native pawn catalog can settle them;
			// unsupported duplicates and inventory shapes remain fail-closed.
			if (const auto owned = transaction.get_inventory(item.item_id); owned &&
				(!pawning || pawning->source.get() != &source ||
					!pawning->items.contains(item.item_id) || !supply_drop_inventory::can_stack(*owned) ||
					std::ranges::any_of(drop.slots, [](const auto& slot) { return slot.dupe_protection; })))
			{
				return true;
			}
			return false;
		});
		return result;
	}

	std::array<unsigned, 5> rarity_weights(const bool common, const int floor, float scale)
	{
		// Historical backend odds are unavailable. These are S2x LOCAL weights:
		// Common 60/26/8/4.5/1.5; improved ordinary 35/38/16/8/3;
		// guaranteed Rare+ 0/65/22/10/3. Higher native floors truncate that table.
		const std::array<unsigned, 5> base = floor ? std::array{0U, 650U, 220U, 100U, 30U} :
			(common ? std::array{600U, 260U, 80U, 45U, 15U} : std::array{350U, 380U, 160U, 80U, 30U});
		scale = std::isfinite(scale) ? std::clamp(scale, 0.5f, 3.0f) : 1.0f;
		std::array<unsigned, 5> weights{};
		for (int rarity = 0; rarity < 5; ++rarity)
		{
			// Native slot rarity is one-based. Scale only the upper three tiers;
			// positive bounded weights retain their relative odds and all floors.
			if (rarity + 1 >= floor)
			{
				weights[rarity] = static_cast<unsigned>(std::lround(base[rarity] * 100 * (rarity >= 2 ? scale : 1.0f)));
			}
		}
		return weights;
	}

	std::optional<std::size_t> select_card(const std::vector<loot_item>& candidates,
		const std::array<unsigned, 5>& weights, std::uint64_t& random_state)
	{
		std::array<std::vector<std::size_t>, 5> tiers;
		for (std::size_t i = 0; i < candidates.size(); ++i)
		{
			const auto rarity = candidates[i].rarity;
			if (rarity >= 0 && rarity < 5 && weights[rarity])
			{
				tiers[rarity].push_back(i);
			}
		}
		unsigned total{};
		for (std::size_t i = 0; i < tiers.size(); ++i)
		{
			if (!tiers[i].empty())
			{
				total += weights[i];
			}
		}
		if (!total)
		{
			return std::nullopt;
		}
		auto roll = next_selection_value(random_state) % total;
		for (std::size_t i = 0; i < tiers.size(); ++i)
		{
			if (tiers[i].empty())
			{
				continue;
			}
			if (roll >= weights[i])
			{
				roll -= weights[i];
				continue;
			}
			return tiers[i][next_selection_value(random_state) % tiers[i].size()];
		}
		return std::nullopt;
	}

	std::optional<std::vector<loot_item>> select_supply_drop_items(
		const supply_drop& drop, std::vector<loot_item> candidates, std::uint64_t random_state,
		const float rarity_scale)
	{
		if (!loot_compatibility::is_confirmed_mp_supply_drop(drop))
		{
			return std::nullopt;
		}
		std::vector<loot_item> selected;
		for (const auto& slot : drop.slots)
		{
			const auto weights = rarity_weights(drop.type == 0, slot.rarity, rarity_scale);
			const auto index = select_card(candidates, weights, random_state);
			if (!index)
			{
				return std::nullopt;
			}
			selected.push_back(std::move(candidates[*index]));
			// Three distinct cards per opening; ownership does not protect against
			// duplicates across openings. Empty rarity tiers are renormalized.
			candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(*index));
		}
		return selected;
	}
}
