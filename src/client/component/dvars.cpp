#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "console/console.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>
#include <utils/io.hpp>

namespace dvars
{
	namespace
	{
		utils::hook::detour dvar_find_malleable_var_hook;

		std::unordered_map<std::string, std::string> dvar_display_name_to_engine_name{};
		std::unordered_map<std::string, std::string> dvar_engine_name_to_display_name{};

		const char* resolve_dvar_engine_name(const char* dvar_name)
		{
			if (!dvar_name || !*dvar_name)
			{
				return dvar_name;
			}

			const std::string name{ dvar_name };

			if (utils::string::is_numeric(name))
			{
				return dvar_name;
			}

			const auto entry = dvar_display_name_to_engine_name.find(name);
			if (entry == dvar_display_name_to_engine_name.end())
			{
				return dvar_name;
			}

			return entry->second.data();
		}

		void load_dvar_name_mappings()
		{
			const auto path = game::get_appdata_path() / "data/lookup_tables/dvar_list.csv";

			std::string data;
			if (!utils::io::read_file(path, &data))
			{
				console::warn("Failed to read dvar name aliases from file: %s\n", path.string().data());
				return;
			}

			const auto [beg, end] = std::ranges::remove(data, '\r');
			data.erase(beg, end);

			std::istringstream stream(data);
			std::string line;

			while (std::getline(stream, line, '\n'))
			{
				utils::string::trim(line);

				if (line.empty() || utils::string::starts_with(line, "//") || utils::string::starts_with(line, "#"))
				{
					continue;
				}

				/*
				Expected CSV format :
				real_name,numeric_alias

				Example:
				mapname,1673
				*/

				const auto comma = line.find(',');
				if (comma == std::string::npos)
				{
					console::warn("Invalid dvar alias line: %s\n", line.data());
					continue;
				}

				auto real_name = line.substr(0, comma);
				auto numeric_alias = line.substr(comma + 1);

				if (real_name.empty() || numeric_alias.empty())
				{
					console::warn("Invalid dvar alias entry: %s\n", line.data());
					continue;
				}

				if (!utils::string::is_numeric(numeric_alias))
				{
					console::warn("Invalid numeric dvar alias '%s' for '%s'\n", numeric_alias.data(), real_name.data());
					continue;
				}

				dvar_display_name_to_engine_name.emplace(real_name, numeric_alias);
				dvar_engine_name_to_display_name.emplace(numeric_alias, real_name);
			}

			console::info("Loaded %zu dvar name mappings\n", dvar_display_name_to_engine_name.size());
		}

		game::dvar_t* dvar_find_malleable_var_stub(const char* dvar_name)
		{
			const auto resolved_name = resolve_dvar_engine_name(dvar_name);
			return dvar_find_malleable_var_hook.invoke<game::dvar_t*>(resolved_name);
		}
	}

	std::string get_dvar_display_name(const std::string& dvar_name)
	{
		if (dvar_name.empty())
		{
			return {};
		}

		if (utils::string::is_numeric(dvar_name))
		{
			const auto entry = dvar_engine_name_to_display_name.find(dvar_name);
			if (entry != dvar_engine_name_to_display_name.end())
			{
				return entry->second;
			}
		}

		return dvar_name;
	}

	std::string get_dvar_engine_name(const std::string& dvar_name)
	{
		if (dvar_name.empty())
		{
			return {};
		}

		if (!utils::string::is_numeric(dvar_name))
		{
			const auto entry = dvar_display_name_to_engine_name.find(dvar_name);
			if (entry != dvar_display_name_to_engine_name.end())
			{
				return entry->second;
			}
		}

		return dvar_name;
	}

	class component final : public generic_component
	{
	public:
		void post_load() override
		{
			load_dvar_name_mappings();
		}

		void post_unpack() override
		{
			dvar_find_malleable_var_hook.create(game::Dvar_FindMalleableVar, dvar_find_malleable_var_stub);
		}
	};
}

REGISTER_COMPONENT(dvars::component)
