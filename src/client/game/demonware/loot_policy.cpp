#include <std_include.hpp>

#include "loot_policy.hpp"
#include "loot_compatibility.hpp"
#include "marketplace_store.hpp"
#include "pawn_catalog.hpp"
#include "supply_drop_inventory.hpp"

#include <cmath>

namespace demonware::loot_policy
{
	using namespace loot_catalog;

	namespace
	{
		constexpr std::size_t rarity_count = 5;

		constexpr std::array<std::string_view, 18> collectible_groups
		{
			"emote",
			"grip",
			"playercard_icon",
			"playercard_title",
			"uniforms",
			"costume",
			"face_camo",
			"weapon_charm",
			"weapon_class_camo",
			"site_reticle",
			"weapon_assault",
			"weapon_smg",
			"weapon_heavy",
			"weapon_sniper",
			"weapon_shotgun",
			"weapon_pistol",
			"weapon_projectile",
			"weapon_other",
		};

		// S2x local weights, the historical backend odds are unknown
		constexpr std::array<unsigned, rarity_count> common_weights{600, 260, 80, 45, 15};
		constexpr std::array<unsigned, rarity_count> improved_weights{350, 380, 160, 80, 30};
		constexpr std::array<unsigned, rarity_count> guaranteed_rare_weights{0, 650, 220, 100, 30};

		std::uint64_t next_selection_value(std::uint64_t& state)
		{
			state += 0x9E3779B97F4A7C15ULL;

			auto value = state;
			value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
			value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;

			return value ^ (value >> 31);
		}

		bool is_table_ready(const table_status& table)
		{
			return table.table_asset_available && table.table_values_available &&
				table.table_dimensions_valid && table.required_columns_available;
		}

		bool is_loot_row(const item_definition& row)
		{
			const auto& item = row.item;
			return std::ranges::find(collectible_groups, item.group) != collectible_groups.end() &&
				item.operation && row.operation_valid && row.loot_flag && row.rarity_eligible &&
				!row.challenge && !row.entitlement && !row.collection_reward;
		}

		bool is_presentable(const item_definition& row)
		{
			const auto& item = row.item;
			return row.localized_name && row.reference_valid && row.presentation_available &&
				row.rarity_valid && item.rarity >= 0 && item.rarity < static_cast<int>(rarity_count) && item.item_id &&
				!item.stock_hidden_item && !item.stock_internal_costume_component && !item.azm_consumable;
		}

		bool is_supply_drop_item(const catalog& source, const std::uint32_t item_id)
		{
			return std::ranges::any_of(source.supply_drops, [item_id](const supply_drop& drop)
			{
				return drop.item_id == item_id;
			});
		}

		bool has_dupe_protection(const supply_drop& drop)
		{
			return std::ranges::any_of(drop.slots, &supply_drop_slot::dupe_protection);
		}

		bool is_eligible(const loot_item& item, const catalog& source, const supply_drop& drop,
			const marketplace_store::transaction& transaction, const pawn_catalog::catalog* pawning)
		{
			if (item.item_id > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
			{
				return false;
			}

			const auto owned = transaction.get_inventory(item.item_id);
			if (!owned)
			{
				return true;
			}

			// Owned cosmetics stay eligible only when the native pawn catalog can settle the duplicate
			return pawning && pawning->source.get() == &source && pawning->items.contains(item.item_id) &&
				supply_drop_inventory::can_stack(*owned) && !has_dupe_protection(drop);
		}

		const std::array<unsigned, rarity_count>& base_weights(const bool common, const int floor)
		{
			if (floor)
			{
				return guaranteed_rare_weights;
			}

			return common ? common_weights : improved_weights;
		}
	}

	std::vector<loot_item> rule_candidates(const catalog& source, const supply_drop* drop)
	{
		std::vector<loot_item> result{};

		if (!is_table_ready(source.source_table) || (drop && !loot_compatibility::is_confirmed_mp_supply_drop(*drop)))
		{
			return result;
		}

		for (const auto& row : source.items)
		{
			if (is_loot_row(row) && is_presentable(row) && !is_supply_drop_item(source, row.item.item_id))
			{
				result.push_back(row.item);
			}
		}

		std::ranges::sort(result, [](const loot_item& left, const loot_item& right)
		{
			return std::tie(left.item_id, left.reference) < std::tie(right.item_id, right.reference);
		});

		const auto duplicates = std::ranges::unique(result, {}, &loot_item::item_id);
		result.erase(duplicates.begin(), duplicates.end());

		return result;
	}

	std::vector<loot_item> eligible_items(const catalog& source, const supply_drop& drop,
		const marketplace_store::transaction& transaction)
	{
		auto result = rule_candidates(source, &drop);
		const auto pawning = pawn_catalog::get_snapshot();

		std::erase_if(result, [&](const loot_item& item)
		{
			return !is_eligible(item, source, drop, transaction, pawning.get());
		});

		return result;
	}

	std::array<unsigned, 5> rarity_weights(const bool common, const int floor, float scale)
	{
		const auto& base = base_weights(common, floor);
		scale = std::isfinite(scale) ? std::clamp(scale, 0.5f, 3.0f) : 1.0f;

		std::array<unsigned, rarity_count> weights{};

		// Native slot rarity is one-based, and only the upper three tiers scale
		for (std::size_t rarity = 0; rarity < rarity_count; ++rarity)
		{
			if (static_cast<int>(rarity) + 1 < floor)
			{
				continue;
			}

			const auto multiplier = rarity >= 2 ? scale : 1.0f;
			weights[rarity] = static_cast<unsigned>(std::lround(base[rarity] * 100 * multiplier));
		}

		return weights;
	}

	std::optional<std::size_t> select_card(const std::vector<loot_item>& candidates,
		const std::array<unsigned, 5>& weights, std::uint64_t& random_state)
	{
		std::array<std::vector<std::size_t>, rarity_count> tiers{};

		for (std::size_t index = 0; index < candidates.size(); ++index)
		{
			const auto rarity = candidates[index].rarity;
			if (rarity >= 0 && rarity < static_cast<int>(rarity_count) && weights[rarity])
			{
				tiers[rarity].push_back(index);
			}
		}

		unsigned total{};
		for (std::size_t rarity = 0; rarity < rarity_count; ++rarity)
		{
			if (!tiers[rarity].empty())
			{
				total += weights[rarity];
			}
		}

		if (!total)
		{
			return std::nullopt;
		}

		auto roll = next_selection_value(random_state) % total;

		for (std::size_t rarity = 0; rarity < rarity_count; ++rarity)
		{
			const auto& tier = tiers[rarity];
			if (tier.empty())
			{
				continue;
			}

			if (roll < weights[rarity])
			{
				return tier[next_selection_value(random_state) % tier.size()];
			}

			roll -= weights[rarity];
		}

		return std::nullopt;
	}

	std::optional<std::vector<loot_item>> select_supply_drop_items(const supply_drop& drop,
		std::vector<loot_item> candidates, std::uint64_t random_state, const float rarity_scale)
	{
		if (!loot_compatibility::is_confirmed_mp_supply_drop(drop))
		{
			return std::nullopt;
		}

		std::vector<loot_item> selected{};

		for (const auto& slot : drop.slots)
		{
			const auto weights = rarity_weights(drop.type == 0, slot.rarity, rarity_scale);

			const auto index = select_card(candidates, weights, random_state);
			if (!index)
			{
				return std::nullopt;
			}

			selected.push_back(std::move(candidates[*index]));
			candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(*index));
		}

		return selected;
	}
}
