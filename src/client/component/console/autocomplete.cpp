#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "autocomplete.hpp"
#include "component/scheduler.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"
#include "game/lookup/dvars.hpp"

#include <utils/string.hpp>

namespace autocomplete
{
	namespace
	{
		constexpr std::size_t max_history = 64;

		struct context
		{
			std::string command;
			std::vector<std::string> args;
			std::size_t arg_index{};
		};

		using provider = std::function<std::vector<match>(const context&)>;

		struct cached_list
		{
			std::mutex mutex;
			std::vector<match> items;
			std::chrono::steady_clock::time_point updated{};
			bool pending{};
		};

		struct cycle_state
		{
			std::string output;
			result base;
		};

		std::unordered_map<std::string, provider> providers;
		std::atomic<std::uint32_t> generation{};

		std::mutex cycle_mutex;
		cycle_state cycle{};

		std::deque<std::string> history;

		const std::unordered_set<std::string> set_commands = {"set", "seta", "sets", "setu"};
		const std::unordered_set<std::string> dvar_commands = {"toggle", "togglep", "reset", "setfromdvar"};

		struct known_map
		{
			const char* name;
			const char* alias;
		};

		// we hardcode this list, but maybe we dont have to :p
		constexpr known_map known_maps[] =
		{
			{"mp_aachen_v2", "Aachen"},
			{"mp_battleship_2", "USS Texas"},
			{"mp_canon_farm", "Gustav Cannon"},
			{"mp_carentan_s2", "Carentan"},
			{"mp_carentan_s2_winter", "Winter Carentan"},
			{"mp_d_day", "Pointe du Hoc"},
			{"mp_flak_tower", "Flak Tower"},
			{"mp_forest_01", "Ardennes"},
			{"mp_france_village", "Sainte Marie du Mont"},
			{"mp_gibraltar_02", "Gibraltar"},
			{"mp_london", "London Docks"},
			{"mp_paris_s2", "Occupation"},
			{"mp_prague", "Anthropoid"},
			{"mp_wolfslair", "Valkyrie"},
			{"mp_wolfslair_free", ""},
			{"mp_shipment_s2", "Shipment 1944"},
			{"mp_dunkirk", "Dunkirk"},
			{"mp_egypt_02", "Egypt"},
			{"mp_v2_rocket_02", "V2"},
			{"mp_market_garden", "Market Garden"},
			{"mp_monte_cassino_v2", "Monte Cassino"},
			{"mp_stalingrad", "Stalingrad"},
			{"mp_airship", ""},
			{"mp_fuhrerbunker", ""},
			{"mp_house", ""},
			{"mp_tank_graveyard_2", ""},
			{"mp_raid_d_day", "Operation Neptune"},
			{"mp_raid_cobra", "Operation Breakout"},
			{"mp_raid_bulge", "Operation Griffin"},
			{"mp_raid_aachen", "Operation Aachen"},
			{"mp_raid_dlc2", ""},
			{"mp_raid_dlc3", ""},
			{"mp_raid_dlc4", ""},
			{"mp_zombie_nest_01", "The Final Reich"},
			{"mp_zombie_house", "Groesten Haus"},
			{"mp_zombie_island", "The Darkest Shore"},
			{"mp_zombie_berlin", "The Shadowed Throne"},
			{"mp_zombie_descent", "The Frozen Dawn"},
			{"mp_zombie_dig_02", ""},
			{"mp_zombie_dnk", ""},
			{"mp_zombie_windmill", ""},
			{"mp_zombie_training", ""},
		};

		bool is_separator(const char c)
		{
			return c == ' ' || c == '\t';
		}

		std::size_t find_segment_start(const std::string& input)
		{
			auto in_quotes = false;
			std::size_t start = 0;

			for (std::size_t i = 0; i < input.size(); i++)
			{
				if (input[i] == '"')
				{
					in_quotes = !in_quotes;
				}
				else if (input[i] == ';' && !in_quotes)
				{
					start = i + 1;
				}
			}

			return start;
		}

		std::vector<std::string> split_words(const std::string& text)
		{
			std::vector<std::string> words;
			std::string current;

			for (const auto c : text)
			{
				if (is_separator(c))
				{
					if (!current.empty())
					{
						words.emplace_back(std::move(current));
						current.clear();
					}
				}
				else
				{
					current.push_back(c);
				}
			}

			if (!current.empty())
			{
				words.emplace_back(std::move(current));
			}

			return words;
		}

		std::string quote_if_needed(const std::string& value)
		{
			if (value.empty() || value.find(' ') != std::string::npos)
			{
				return "\"" + value + "\"";
			}

			return value;
		}

		std::string dvar_get_name(const game::dvar_t* dvar)
		{
			return dvar->name ? std::string(game::lookup::dvars::resolve_display_name(dvar->name)) : std::string{};
		}

		std::string dvar_value(const game::dvar_t* dvar, const game::DvarValue& value)
		{
			auto* const mutable_dvar = const_cast<game::dvar_t*>(dvar);
			const auto* str = game::Dvar_ValueToString(mutable_dvar, &value == &dvar->current,
				const_cast<game::DvarValue*>(&value));
			return str ? str : "";
		}

		std::vector<match> get_cached(cached_list& list, const std::chrono::milliseconds max_age,
			const scheduler::pipeline pipeline, std::vector<match>(*refresh)())
		{
			std::lock_guard _(list.mutex);

			const auto now = std::chrono::steady_clock::now();
			if (!list.pending && (list.updated == std::chrono::steady_clock::time_point{} || now - list.updated > max_age))
			{
				list.pending = true;
				scheduler::once([&list, refresh]
				{
					std::vector<match> items{};
					try
					{
						items = refresh();
					}
					catch (...)
					{
					}

					std::lock_guard lock(list.mutex);
					list.items = std::move(items);
					list.updated = std::chrono::steady_clock::now();
					list.pending = false;
					++generation;
				}, pipeline);
			}

			return list.items;
		}

		std::vector<match> get_dvar_names()
		{
			std::vector<match> names;

			for (auto i = 0; i < *game::dvarCount; i++)
			{
				const auto* dvar = &game::dvarPool[i];

				auto name = dvar_get_name(dvar);
				if (!name.empty())
				{
					names.emplace_back(std::move(name), dvar_value(dvar, dvar->current), match_type::dvar);
				}
			}

			return names;
		}

		std::vector<match> get_dvar_values(const game::dvar_t* dvar)
		{
			std::vector<match> values;

			const auto current = dvar_value(dvar, dvar->current);
			const auto reset = dvar_value(dvar, dvar->reset);

			const auto describe = [&](const std::string& value, const char* fallback = "")
			{
				if (value == current && value == reset) return "current, default";
				if (value == current) return "current";
				if (value == reset) return "default";
				return fallback;
			};

			switch (dvar->type)
			{
			case game::DvarType::DVAR_TYPE_BOOL:
				if (dvar->current.enabled)
				{
					values.emplace_back("0", describe("0", "false"), match_type::argument);
					values.emplace_back("1", describe("1", "true"), match_type::argument);
				}
				else
				{
					values.emplace_back("1", describe("1", "true"), match_type::argument);
					values.emplace_back("0", describe("0", "false"), match_type::argument);
				}
				break;
			case game::DvarType::DVAR_TYPE_ENUM:
				for (auto i = 0; i < dvar->domain.enumeration.stringCount; i++)
				{
					const auto* value = dvar->domain.enumeration.strings[i];
					if (value)
					{
						values.emplace_back(quote_if_needed(value), describe(value), match_type::argument);
					}
				}
				break;
			default:
				values.emplace_back(quote_if_needed(current), current == reset ? "current, default" : "current", match_type::argument);
				if (current != reset)
				{
					values.emplace_back(quote_if_needed(reset), "default", match_type::argument);
				}
				break;
			}

			return values;
		}

		const std::unordered_set<std::string> list_hidden_commands =
		{
			"map", "devmap", "ui_mapname",
		};

		std::vector<match> refresh_maps()
		{
			std::vector<match> maps;
			std::error_code ec;

			for (const auto& map : known_maps)
			{
				maps.emplace_back(map.name, map.alias, match_type::argument);
			}

			for (const auto& entry : std::filesystem::directory_iterator("usermaps", ec))
			{
				const auto name = utils::string::to_lower(entry.path().filename().string());
				if (entry.is_directory(ec) && std::filesystem::exists(entry.path() / (name + ".ff"), ec) &&
					std::ranges::none_of(maps, [&](const match& map) { return map.name == name; }))
				{
					maps.emplace_back(name, "", match_type::argument);
				}
			}

			return maps;
		}

		std::vector<match> refresh_gametypes()
		{
			std::vector<match> gametypes;

			constexpr auto table_name = "mp/gameTypesTable.csv";
			if (!game::DB_XAssetExists(game::ASSET_TYPE_STRINGTABLE, table_name))
			{
				return gametypes;
			}

			const auto* table = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE, table_name, false).stringTable;
			if (!table || !table->values)
			{
				return gametypes;
			}

			for (auto row = 1; row < table->rowCount; row++)
			{
				const auto* name = table->values[row * table->columnCount].string;
				if (name && *name)
				{
					gametypes.emplace_back(name, "", match_type::argument);
				}
			}

			return gametypes;
		}

		void collect_weapon(const game::XAssetHeader header, void* data)
		{
			auto& weapons = *static_cast<std::vector<match>*>(data);
			const game::XAsset asset{game::ASSET_TYPE_WEAPON, header};
			const auto* name = game::DB_GetXAssetName(&asset);
			if (name && *name)
			{
				weapons.emplace_back(name, "", match_type::argument);
			}
		}

		std::vector<match> refresh_weapons()
		{
			std::vector<match> weapons;
			game::DB_EnumXAssets_FastFile(game::ASSET_TYPE_WEAPON, collect_weapon, &weapons, true);

			std::ranges::sort(weapons, {}, &match::name);
			return weapons;
		}

		cached_list maps_cache;
		cached_list gametypes_cache;
		cached_list weapons_cache;

		std::vector<match> get_maps()
		{
			auto maps = get_cached(maps_cache, 30s, scheduler::pipeline::async, refresh_maps);

			const auto zombies = game::environment::is_zombies();
			std::ranges::stable_partition(maps, [zombies](const match& map)
			{
				return map.name.starts_with("mp_zombie_") == zombies;
			});

			return maps;
		}

		std::vector<match> get_gametypes()
		{
			return get_cached(gametypes_cache, 10s, scheduler::pipeline::main, refresh_gametypes);
		}

		std::vector<match> get_weapons()
		{
			return get_cached(weapons_cache, 5s, scheduler::pipeline::main, refresh_weapons);
		}

		std::vector<match> get_keys()
		{
			std::vector<match> keys;
			if (!game::Key_KeynumToString.get())
			{
				return keys;
			}

			std::unordered_set<std::string> seen;

			for (auto i = 0; i < 256; i++)
			{
				const auto* name = game::Key_KeynumToString(i, 0, 1);
				if (!name || !*name || !strncmp(name, "0x", 2) || seen.contains(name))
				{
					continue;
				}

				seen.insert(name);
				keys.emplace_back(name, "", match_type::argument);
			}

			return keys;
		}

		std::vector<match> get_dvar_arguments(const std::string& dvar_name, const game::dvar_t* dvar)
		{
			if (dvar_name == "g_gametype" || dvar_name == "ui_gametype")
			{
				return get_gametypes();
			}

			if (dvar_name == "ui_mapname")
			{
				return get_maps();
			}

			return get_dvar_values(dvar);
		}

		std::vector<match> get_arguments(const context& ctx)
		{
			if (const auto itr = providers.find(ctx.command); itr != providers.end())
			{
				return itr->second(ctx);
			}

			if (ctx.arg_index == 1)
			{
				if (const auto* dvar = game::Dvar_FindMalleableVar(ctx.command.data()))
				{
					return get_dvar_arguments(ctx.command, dvar);
				}
			}

			return {};
		}

		std::vector<match> filter(std::vector<match> candidates, const std::string& token, const bool keep_order)
		{
			const auto lower_token = utils::string::to_lower(token);

			std::vector<match> prefix;
			std::vector<match> contains;

			for (auto& candidate : candidates)
			{
				const auto lower_name = utils::string::to_lower(candidate.name);
				const auto pos = lower_name.find(lower_token);

				if (pos == 0)
				{
					prefix.emplace_back(std::move(candidate));
				}
				else if (pos != std::string::npos)
				{
					contains.emplace_back(std::move(candidate));
				}
			}

			if (!keep_order)
			{
				const auto by_name = [](const match& a, const match& b)
				{
					return _stricmp(a.name.data(), b.name.data()) < 0;
				};

				std::ranges::sort(prefix, by_name);
				std::ranges::sort(contains, by_name);
			}

			prefix.insert(prefix.end(), std::make_move_iterator(contains.begin()), std::make_move_iterator(contains.end()));
			return prefix;
		}

		std::vector<match> find_commands(const std::string& token)
		{
			auto candidates = get_dvar_names();

			for (auto* cmd = *game::cmd_functions; cmd; cmd = cmd->next)
			{
				if (cmd->name && *cmd->name)
				{
					candidates.emplace_back(cmd->name, "", match_type::command);
				}
			}

			return filter(std::move(candidates), token, false);
		}

		result build_result(const std::string& input)
		{
			result result{};

			const auto segment_start = find_segment_start(input);
			auto command_start = segment_start;

			while (command_start < input.size() && is_separator(input[command_start]))
			{
				command_start++;
			}

			if (command_start < input.size() && (input[command_start] == '/' || input[command_start] == '\\'))
			{
				command_start++;
			}

			const auto segment = input.substr(command_start);
			auto words = split_words(segment);

			const auto ends_with_space = !segment.empty() && is_separator(segment.back());
			if (ends_with_space || words.empty())
			{
				result.token.clear();
			}
			else
			{
				result.token = words.back();
				words.pop_back();
			}

			result.head = input.substr(0, input.size() - result.token.size());
			result.arg_index = words.size();

			if (result.arg_index == 0)
			{
				if (!result.token.empty())
				{
					result.command = result.token;
					result.matches = find_commands(result.token);
				}

				return result;
			}

			result.command = utils::string::to_lower(words.front());

			context ctx{};
			ctx.command = result.command;
			ctx.args.assign(words.begin() + 1, words.end());
			ctx.arg_index = result.arg_index;

			result.matches = filter(get_arguments(ctx), result.token, true);
			result.show_list = !list_hidden_commands.contains(result.command);
			return result;
		}

		bool should_append_space(const result& result)
		{
			if (result.arg_index == 0)
			{
				return true;
			}

			return result.arg_index == 1 && (set_commands.contains(result.command) || result.command == "setfromdvar" ||
				result.command == "bind");
		}

		void register_providers()
		{
			const auto dvar_name_provider = [](const context& ctx) -> std::vector<match>
			{
				if (ctx.arg_index == 1 || (ctx.command == "setfromdvar" && ctx.arg_index == 2))
				{
					return get_dvar_names();
				}

				if (ctx.arg_index == 2 && set_commands.contains(ctx.command))
				{
					const auto dvar_name = utils::string::to_lower(ctx.args[0]);
					if (const auto* dvar = game::Dvar_FindMalleableVar(dvar_name.data()))
					{
						return get_dvar_arguments(dvar_name, dvar);
					}
				}

				return {};
			};

			for (const auto& command : set_commands)
			{
				providers[command] = dvar_name_provider;
			}

			for (const auto& command : dvar_commands)
			{
				providers[command] = dvar_name_provider;
			}

			const auto map_provider = [](const context& ctx) -> std::vector<match>
			{
				return ctx.arg_index == 1 ? get_maps() : std::vector<match>{};
			};

			providers["map"] = map_provider;
			providers["devmap"] = map_provider;

			providers["give"] = [](const context& ctx) -> std::vector<match>
			{
				return ctx.arg_index == 1 ? get_weapons() : std::vector<match>{};
			};

			providers["take"] = providers["give"];

			providers["listassetpool"] = [](const context& ctx) -> std::vector<match>
			{
				std::vector<match> pools;
				if (ctx.arg_index != 1)
				{
					return pools;
				}

				for (auto i = 0; i < game::ASSET_TYPE_COUNT; i++)
				{
					const auto* name = game::DB_GetXAssetTypeName(static_cast<game::XAssetType>(i));
					pools.emplace_back(std::to_string(i), name ? name : "", match_type::argument);
				}

				return pools;
			};

			const auto key_provider = [](const context& ctx) -> std::vector<match>
			{
				return ctx.arg_index == 1 ? get_keys() : std::vector<match>{};
			};

			providers["bind"] = key_provider;
			providers["unbind"] = key_provider;
		}
	}

	result query(const std::string& input)
	{
		{
			std::lock_guard _(cycle_mutex);
			if (!cycle.output.empty() && input == cycle.output)
			{
				return cycle.base;
			}
		}

		return build_result(input);
	}

	std::string get_ghost_text(const result& result)
	{
		if (result.selected >= 0 || result.matches.empty() || (result.token.empty() && result.arg_index == 0))
		{
			return {};
		}

		const auto& best = result.matches.front().name;
		if (best.size() <= result.token.size() || _strnicmp(best.data(), result.token.data(), result.token.size()) != 0)
		{
			return {};
		}

		return best.substr(result.token.size());
	}

	std::string complete(const std::string& input, const bool reverse)
	{
		std::lock_guard _(cycle_mutex);

		if (!cycle.output.empty() && input == cycle.output && !cycle.base.matches.empty())
		{
			const auto count = static_cast<int>(cycle.base.matches.size());
			cycle.base.selected = (cycle.base.selected + (reverse ? -1 : 1) + count) % count;
			cycle.output = cycle.base.head + cycle.base.matches[cycle.base.selected].name;
			return cycle.output;
		}

		cycle = {};

		auto base = build_result(input);
		if (base.matches.empty())
		{
			return input;
		}

		if (base.matches.size() == 1)
		{
			auto output = base.head + base.matches.front().name;
			if (should_append_space(base))
			{
				output.push_back(' ');
			}

			return output;
		}

		base.selected = reverse ? static_cast<int>(base.matches.size()) - 1 : 0;
		cycle.output = base.head + base.matches[base.selected].name;
		cycle.base = std::move(base);

		return cycle.output;
	}

	std::string prepare_command(const std::string& input)
	{
		auto start = input.find_first_not_of(" \t");
		if (start == std::string::npos)
		{
			return {};
		}

		if (input[start] == '/' || input[start] == '\\')
		{
			start++;
		}

		const auto command = input.substr(start);
		if (command.find('"') != std::string::npos)
		{
			return command;
		}

		const auto to_bool_value = [](const std::string& value) -> const char*
		{
			const auto lower = utils::string::to_lower(value);
			if (lower == "true" || lower == "on" || lower == "yes") return "1";
			if (lower == "false" || lower == "off" || lower == "no") return "0";
			return nullptr;
		};

		const auto is_bool_dvar = [](const std::string& name)
		{
			const auto* dvar = game::Dvar_FindMalleableVar(name.data());
			return dvar && dvar->type == game::DvarType::DVAR_TYPE_BOOL;
		};

		std::string output;
		for (const auto& segment : utils::string::split(command, ';'))
		{
			if (!output.empty())
			{
				output.push_back(';');
			}

			auto words = split_words(segment);
			auto changed = false;

			std::size_t value_index = 1;
			if (words.size() >= 3 && set_commands.contains(utils::string::to_lower(words[0])))
			{
				value_index = 2;
			}

			if (words.size() == value_index + 1 && is_bool_dvar(words[value_index - 1]))
			{
				if (const auto* value = to_bool_value(words[value_index]))
				{
					words[value_index] = value;
					changed = true;
				}
			}

			if (!changed)
			{
				output.append(segment);
				continue;
			}

			for (std::size_t i = 0; i < words.size(); i++)
			{
				output.append(i ? " " : "").append(words[i]);
			}
		}

		return output;
	}

	std::uint32_t get_generation()
	{
		return generation;
	}

	void add_history(const std::string& input)
	{
		if (input.empty())
		{
			return;
		}

		std::erase(history, input);
		history.push_front(input);

		if (history.size() > max_history)
		{
			history.pop_back();
		}
	}

	const std::deque<std::string>& get_history()
	{
		return history;
	}

	void clear_history()
	{
		history.clear();
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated())
			{
				return;
			}

			register_providers();
		}
	};
}

REGISTER_COMPONENT(autocomplete::component)
