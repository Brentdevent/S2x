#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"

#include "game/game.hpp"
#include "game/lookup/dvars.hpp"

#include "console/console.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>

namespace dvars
{
	namespace
	{
		utils::hook::detour dvar_find_malleable_var_hook;
		utils::hook::detour dvar_register_hook;

		const char* resolve_dvar_engine_name(const char* dvar_name)
		{
			if (!dvar_name || !*dvar_name)
			{
				return dvar_name;
			}

			const std::string_view name{ dvar_name };
			const auto resolved_name = game::lookup::dvars::resolve_engine_name(name);
			return resolved_name == name ? dvar_name : resolved_name.data();
		}

		game::dvar_t* dvar_find_malleable_var_stub(const char* dvar_name)
		{
			const auto resolved_name = resolve_dvar_engine_name(dvar_name);
			return dvar_find_malleable_var_hook.invoke<game::dvar_t*>(resolved_name);
		}

		game::dvar_t* dvar_register_string_stub(const char* dvar_name, const char* value, const game::DvarFlags flags)
		{
			// Early config writes register missing dvars using the caller's original name.
			const auto resolved_name = resolve_dvar_engine_name(dvar_name);
			return dvar_register_hook.invoke<game::dvar_t*>(resolved_name, value, flags);
		}

		game::dvar_t* dvar_register_variant_stub(const char* dvar_name, const game::DvarType type,
			const game::DvarFlags flags, game::DvarValue* value, game::DvarLimits* domain)
		{
			// SP inlines string registration in early set/config commands.
			return dvar_register_hook.invoke<game::dvar_t*>(resolve_dvar_engine_name(dvar_name), type, flags, value, domain);
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			dvar_find_malleable_var_hook.create(game::Dvar_FindMalleableVar, dvar_find_malleable_var_stub);
			if (game::environment::is_singleplayer())
			{
				dvar_register_hook.create(game::Dvar_RegisterVariant, dvar_register_variant_stub);
			}
			else
			{
				dvar_register_hook.create(game::Dvar_RegisterString, dvar_register_string_stub);
			}

			command::add("dvarDump", [](const command::params& argument)
			{
				console::info("================================ DVAR DUMP ========================================\n");

				std::string filename;
				if (argument.size() == 2)
				{
					filename = "s2x/";
					filename.append(argument[1]);

					if (!filename.ends_with(".txt"))
					{
						filename.append(".txt");
					}
				}

				for (auto i = 0; i < *game::dvarCount; i++)
				{
					auto* dvar = &game::dvarPool[i];

					if (dvar)
					{
						if (!filename.empty())
						{
							const auto line = std::format("{} \"{}\"\r\n", dvar->name, game::Dvar_ValueToString(dvar, true, &dvar->current));
							utils::io::write_file(filename, line, i != 0);
						}

						console::info("%s \"%s\"\n", dvar->name, game::Dvar_ValueToString(dvar, true, &dvar->current));
					}
				}

				console::info("\n%i total dvars.\n", *game::dvarCount);
				console::info("================================ END DVAR DUMP ====================================\n");
			});
		}
	};
}

REGISTER_COMPONENT(dvars::component)
