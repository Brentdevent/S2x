#include <std_include.hpp>

#include "loot_catalog.hpp"

#include "game/game.hpp"
#include "game/string_table.hpp"

#include <mutex>
#include <random>

namespace demonware::loot_catalog
{
	namespace
	{
		constexpr auto rarity_count = 5;

		constexpr auto drop_name_column = 0;
		constexpr auto drop_guid_column = 5;
		constexpr auto drop_zombies_column = 11;
		constexpr auto drop_min_rarity_column = 12;
		constexpr auto drop_focus_column = 13;

		constexpr auto item_category_column = 0;
		constexpr auto item_reference_column = 2;
		constexpr auto item_image_column = 4;
		constexpr auto item_guid_column = 18;
		constexpr auto item_droppable_column = 22;
		constexpr auto item_rarity_column = 29;

		enum focus : int
		{
			focus_none = 0,
			focus_camo = 1,
			focus_helmet = 2,
			focus_uniform = 3,
			focus_weapon = 4,
			focus_charm = 6,
			focus_emote = 7,
			focus_reticle = 8,
			focus_rifle = 10,
			focus_smg = 11,
			focus_lmg = 12,
			focus_sniper = 13,
			focus_shotgun = 14,
			focus_zombie_consumable = 23,
		};

		struct item
		{
			std::uint32_t guid{};
			int rarity{};
			std::string_view category{};
			bool helmet{};
		};

		struct catalog
		{
			std::unordered_map<std::string, drop_type> drops{};
			std::vector<item> items{};
			std::vector<item> zombie_consumables{};
		};

		std::mutex catalog_mutex{};
		std::optional<catalog> loaded_catalog{};

		bool parse_int(const char* text, int& value)
		{
			return game::string_table::parse_integer(text, value);
		}

		bool parse_guid(const char* text, std::uint32_t& value)
		{
			if (!text || !*text)
			{
				return false;
			}

			char* end{};
			const auto result = std::strtoul(text, &end, 0);
			if (end == text || *end || !result || result > std::numeric_limits<std::uint32_t>::max())
			{
				return false;
			}

			value = static_cast<std::uint32_t>(result);
			return true;
		}

		bool is_weapon(const std::string_view category)
		{
			return category == "weapon_assault" || category == "weapon_smg" || category == "weapon_heavy" ||
				category == "weapon_sniper" || category == "weapon_shotgun" || category == "weapon_pistol" ||
				category == "weapon_other" || category == "weapon_projectile";
		}

		bool is_generic_category(const std::string_view category)
		{
			return is_weapon(category) || category == "uniforms" || category == "emote" ||
				category == "face_camo" || category == "grip" || category == "site_reticle" ||
				category == "universal_camo" || category == "weapon_charm" || category == "weapon_class_camo";
		}

		bool matches_focus(const item& entry, const int value)
		{
			switch (value)
			{
			case focus_camo:
				return entry.category == "weapon_class_camo" || entry.category == "universal_camo";
			case focus_helmet:
				return entry.helmet;
			case focus_uniform:
				return entry.category == "uniforms";
			case focus_weapon:
				return is_weapon(entry.category);
			case focus_charm:
				return entry.category == "weapon_charm";
			case focus_emote:
				return entry.category == "emote";
			case focus_reticle:
				return entry.category == "site_reticle";
			case focus_rifle:
				return entry.category == "weapon_assault";
			case focus_smg:
				return entry.category == "weapon_smg";
			case focus_lmg:
				return entry.category == "weapon_heavy";
			case focus_sniper:
				return entry.category == "weapon_sniper";
			case focus_shotgun:
				return entry.category == "weapon_shotgun";
			default:
				return !entry.helmet;
			}
		}

		std::string_view intern_category(const char* category)
		{
			static std::unordered_set<std::string> categories{};
			return *categories.emplace(category).first;
		}

		int roll_rarity(std::mt19937& engine, const int min_rarity)
		{
			constexpr std::array<int, rarity_count> weights{55, 28, 11, 5, 1};
			std::discrete_distribution<int> distribution{weights.begin() + min_rarity, weights.end()};
			return min_rarity + distribution(engine);
		}

		std::uint32_t pick(std::mt19937& engine, const std::vector<const item*>& pool, const int rarity)
		{
			for (auto distance = 0; distance < rarity_count; ++distance)
			{
				for (const auto candidate_rarity : {rarity - distance, rarity + distance})
				{
					std::vector<const item*> candidates{};
					for (const auto* entry : pool)
					{
						if (entry->rarity == candidate_rarity)
						{
							candidates.push_back(entry);
						}
					}

					if (!candidates.empty())
					{
						std::uniform_int_distribution<std::size_t> index{0, candidates.size() - 1};
						return candidates[index(engine)]->guid;
					}
				}
			}

			return 0;
		}
	}

	bool load()
	{
		{
			std::lock_guard lock{catalog_mutex};
			if (loaded_catalog)
			{
				return true;
			}
		}

		const auto* drops = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,
			"mp/supplyDropTypes.csv", false).stringTable;
		const auto* stats = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,
			"mp/statstable.csv", false).stringTable;
		if (!drops || !stats || drops->rowCount <= 1 || stats->rowCount <= 1)
		{
			return false;
		}

		catalog result{};
		for (auto row = 1; row < drops->rowCount; ++row)
		{
			const auto* name = game::string_table::get_cell(drops, row, drop_name_column);
			drop_type drop{};
			int guid{};
			if (!name || !*name || !parse_int(game::string_table::get_cell(drops, row, drop_guid_column), guid) ||
				guid <= 0)
			{
				continue;
			}

			drop.guid = static_cast<std::uint32_t>(guid);
			int value{};
			if (parse_int(game::string_table::get_cell(drops, row, drop_min_rarity_column), value) && value > 0)
			{
				drop.min_rarity = std::clamp(value - 1, 0, rarity_count - 1);
			}

			if (parse_int(game::string_table::get_cell(drops, row, drop_focus_column), value))
			{
				drop.focus = value;
			}

			drop.zombies = parse_int(game::string_table::get_cell(drops, row, drop_zombies_column), value) && value;
			result.drops.emplace(name, drop);
		}

		for (auto row = 1; row < stats->rowCount; ++row)
		{
			const auto* category = game::string_table::get_cell(stats, row, item_category_column);
			item entry{};
			if (!category || !parse_guid(game::string_table::get_cell(stats, row, item_guid_column), entry.guid) ||
				!parse_int(game::string_table::get_cell(stats, row, item_rarity_column), entry.rarity) ||
				entry.rarity < 0 || entry.rarity >= rarity_count)
			{
				continue;
			}

			const std::string_view category_name{category};
			if (category_name == "zombieconsumable")
			{
				entry.category = intern_category(category);
				result.zombie_consumables.push_back(entry);
				continue;
			}

			int droppable{};
			if (!parse_int(game::string_table::get_cell(stats, row, item_droppable_column), droppable) || !droppable)
			{
				continue;
			}

			if (category_name == "costume")
			{
				const auto* image = game::string_table::get_cell(stats, row, item_image_column);
				const auto* reference = game::string_table::get_cell(stats, row, item_reference_column);
				entry.helmet = (image && std::strstr(image, "helmet")) || (reference && std::strncmp(reference, "hat", 3) == 0);
				if (!entry.helmet)
				{
					continue;
				}
			}
			else if (!is_generic_category(category_name))
			{
				continue;
			}

			entry.category = intern_category(category);
			result.items.push_back(entry);
		}

		if (result.drops.empty() || result.items.empty())
		{
			return false;
		}

		std::lock_guard lock{catalog_mutex};
		loaded_catalog = std::move(result);
		return true;
	}

	bool is_loaded()
	{
		std::lock_guard lock{catalog_mutex};
		return loaded_catalog.has_value();
	}

	std::optional<drop_type> find_drop(const std::string_view name)
	{
		std::lock_guard lock{catalog_mutex};
		if (!loaded_catalog)
		{
			return std::nullopt;
		}

		const auto entry = loaded_catalog->drops.find(std::string{name});
		if (entry == loaded_catalog->drops.end())
		{
			return std::nullopt;
		}

		return entry->second;
	}

	std::optional<std::uint32_t> find_drop_guid(const std::string_view name)
	{
		const auto drop = find_drop(name);
		if (!drop)
		{
			return std::nullopt;
		}

		return drop->guid;
	}

	std::vector<std::uint32_t> roll_drop(const drop_type& drop, const std::size_t count)
	{
		std::lock_guard lock{catalog_mutex};
		std::vector<std::uint32_t> result{};
		if (!loaded_catalog)
		{
			return result;
		}

		std::vector<const item*> pool{};
		for (const auto& entry : loaded_catalog->items)
		{
			if (drop.focus != focus_zombie_consumable && matches_focus(entry, drop.focus))
			{
				pool.push_back(&entry);
			}
		}

		if (drop.zombies || drop.focus == focus_zombie_consumable)
		{
			for (const auto& entry : loaded_catalog->zombie_consumables)
			{
				pool.push_back(&entry);
			}
		}

		if (pool.empty())
		{
			return result;
		}

		static std::mt19937 engine{std::random_device{}()};
		for (std::size_t i = 0; i < count; ++i)
		{
			const auto rarity = roll_rarity(engine, i == 0 ? drop.min_rarity : 0);
			if (const auto guid = pick(engine, pool, rarity))
			{
				result.push_back(guid);
			}
		}

		return result;
	}
}
