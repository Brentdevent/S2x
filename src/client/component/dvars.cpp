#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "dvars.hpp"

#include "game/game.hpp"
#include "game/lookup/dvars.hpp"

#include "console/console.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>

namespace dvars
{
	namespace
	{
		struct dvar_float
		{
			float value;
			float min;
			float max;
			game::DvarFlags flags;
			bool local;
		};

		auto& register_float_overrides()
		{
			static std::unordered_map<std::string, dvar_float> overrides;
			return overrides;
		}

		std::string override_key(const std::string_view name)
		{
			return utils::string::to_lower(std::string{game::lookup::dvars::resolve_engine_name(name)});
		}

		const dvar_float* find_float_override(const char* name)
		{
			if (!name)
			{
				return nullptr;
			}

			const auto& overrides = register_float_overrides();
			const auto entry = overrides.find(override_key(name));
			return entry != overrides.end() ? &entry->second : nullptr;
		}

		utils::hook::detour dvar_find_malleable_var_hook;
		utils::hook::detour dvar_register_hook;
		utils::hook::detour dvar_register_float_hook;
		utils::hook::detour dvar_register_float_protected_hook;

		game::dvar_t* register_float(utils::hook::detour& hook, const char* name, float value,
			float min, float max, game::DvarFlags flags)
		{
			if (const auto* var = find_float_override(name))
			{
				value = var->value;
				min = var->min;
				max = var->max;
				flags = var->flags;
			}

			return hook.invoke<game::dvar_t*>(name, value, min, max, flags);
		}

		game::dvar_t* dvar_register_float_stub(const char* name, const float value, const float min,
			const float max, const game::DvarFlags flags)
		{
			return register_float(dvar_register_float_hook, name, value, min, max, flags);
		}

		game::dvar_t* dvar_register_float_protected_stub(const char* name, const float value, const float min,
			const float max, const game::DvarFlags flags)
		{
			// Keep MP's protected registration and value encoding in the engine.
			return register_float(dvar_register_float_protected_hook, name, value, min, max, flags);
		}

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

	namespace override
	{
		void register_float(const std::string& name, const float value, const float min, const float max,
			const game::DvarFlags flags)
		{
			register_float_overrides()[override_key(name)] = {value, min, max, flags, false};
		}

		void register_local_float(const std::string& name, const float value, const float min, const float max)
		{
			// Exact SAVED flags exclude both NETWORK and REPLICATED. The same entry
			// controls registration and script filtering; SAVED alone does not imply local ownership.
			register_float_overrides()[override_key(name)] = {value, min, max, game::DVAR_FLAG_SAVED, true};
		}

		bool is_local(const char* name)
		{
			const auto* var = find_float_override(name);
			return var && var->local;
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			dvar_find_malleable_var_hook.create(game::Dvar_FindMalleableVar, dvar_find_malleable_var_stub);
			dvar_register_float_hook.create(game::Dvar_RegisterFloat, dvar_register_float_stub);
			if (game::environment::is_singleplayer())
			{
				dvar_register_hook.create(game::Dvar_RegisterVariant, dvar_register_variant_stub);
			}
			else
			{
				dvar_register_hook.create(game::Dvar_RegisterString, dvar_register_string_stub);
				dvar_register_float_protected_hook.create(game::Dvar_RegisterFloatProtected, dvar_register_float_protected_stub);
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
