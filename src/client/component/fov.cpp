#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include <utils/finally.hpp>
#include <utils/hook.hpp>

namespace fov
{
	namespace
	{
		constexpr auto maximum_fov = 160.0f;
		constexpr auto no_scale_override = std::numeric_limits<float>::quiet_NaN();

		std::atomic<float> desired_scale{no_scale_override};
		thread_local bool registering_scale = false;

		utils::hook::detour register_float_hook;
		utils::hook::detour get_fov_range_hook;
		utils::hook::detour set_variant_hook;
		utils::hook::detour reset_hook;

		bool is_scale(const char* name)
		{
			return name && !std::strcmp(name, "3078");
		}

		bool is_fov(const char* name)
		{
			return name && (!_stricmp(name, "cg_fov") || !_stricmp(name, "cg_fov1"));
		}

		bool is_float(const game::dvar_t* dvar)
		{
			return dvar && (dvar->type == game::DVAR_TYPE_FLOAT || dvar->type == game::DVAR_TYPE_FLOAT_PROTECTED);
		}

		float current_float(const game::dvar_t* dvar)
		{
			return dvar->type == game::DVAR_TYPE_FLOAT ? dvar->current.value : game::Dvar_DecodeFloat(dvar);
		}

		bool valid_scale(const game::dvar_t* dvar, const float value)
		{
			return std::isfinite(value) && value >= dvar->domain.value.min && value <= dvar->domain.value.max;
		}

		game::dvar_t* register_float_stub(const char* name, const float value, const float min,
			const float max, const game::DvarFlags flags)
		{
			if (!is_scale(name))
			{
				// Register through the engine so its protected-pool bookkeeping remains intact.
				return register_float_hook.invoke<game::dvar_t*>(name, value, min, is_fov(name) ? maximum_fov : max, flags);
			}

			auto requested = desired_scale.load();
			auto* previous = game::Dvar_FindMalleableVar(name);
			if (previous && previous->type == game::DVAR_TYPE_STRING && (previous->flags & game::DVAR_FLAG_EXTERNAL))
			{
				// +set / configs can create a string before stock registers the float.
				// Stock reregistration can reset an archived value to the default.
				requested = game::Dvar_GetFloat(name);

				// Select stock's external-string conversion path, which permits the new NETWORK flag.
				// Dvar_Reregister replaces these temporary flags with the stock registration flags.
				if (flags & game::DVAR_FLAG_NETWORK)
				{
					game::Dvar_SetFlags(previous, game::DVAR_FLAG_SAVED);
				}
			}

			game::dvar_t* dvar{};
			{
				const auto previous_registering = std::exchange(registering_scale, true);
				const auto restore = utils::finally([previous_registering] { registering_scale = previous_registering; });
				dvar = register_float_hook.invoke<game::dvar_t*>(name, value, min, max, flags);
			}

			if (is_float(dvar) && valid_scale(dvar, requested))
			{
				game::DvarValue user_value{};
				user_value.value = requested;
				set_variant_hook.invoke<void>(dvar, &user_value, game::DVAR_SOURCE_INTERNAL);
				if (current_float(dvar) == requested)
				{
					desired_scale.store(requested);
					game::Dvar_SetFlags(dvar, game::DVAR_FLAG_SAVED);
				}
			}

			return dvar;
		}

		bool get_fov_range_stub(int* min, int* max)
		{
			// Shared by the stock domain callback, Settings list, and mode-change adjustment.
			const auto result = get_fov_range_hook.invoke<bool>(min, max);
			*max = static_cast<int>(maximum_fov);
			return result;
		}

		void set_variant_stub(game::dvar_t* dvar, game::DvarValue* value, const game::DvarSetSource source)
		{
			if (!is_float(dvar))
			{
				set_variant_hook.invoke<void>(dvar, value, source);
				return;
			}

			if (is_scale(dvar->name) && !registering_scale)
			{
				if (source == game::DVAR_SOURCE_EXTERNAL)
				{
					// Only the explicit user scale bypasses the stock CHEAT restriction.
					// Dvar_SetVariant takes plaintext and performs the keyed encoding itself.
					set_variant_hook.invoke<void>(dvar, value, game::DVAR_SOURCE_INTERNAL);
					if (valid_scale(dvar, value->value) && current_float(dvar) == value->value)
					{
						desired_scale.store(value->value);
						// Archive accepted user choices through the normal engine config writer.
						game::Dvar_SetFlags(dvar, game::DVAR_FLAG_SAVED);
					}
					return;
				}

				if (std::isfinite(desired_scale.load()))
				{
					return;
				}
			}

			if (is_fov(dvar->name) && source == game::DVAR_SOURCE_EXTERNAL && std::isfinite(value->value))
			{
				// The stock setter rejects out-of-domain floats; clamp user input before encoding.
				int min{}, max{};
				game::CG_GetFovRange(&min, &max);
				const auto lower = std::max(dvar->domain.value.min, static_cast<float>(min));
				const auto upper = std::min(dvar->domain.value.max, static_cast<float>(max));
				if (lower <= upper)
				{
					auto clamped = *value;
					clamped.value = std::clamp(value->value, lower, upper);
					set_variant_hook.invoke<void>(dvar, &clamped, source);
					return;
				}
			}

			set_variant_hook.invoke<void>(dvar, value, source);
		}

		void reset_stub(game::dvar_t* dvar, const game::DvarSetSource source)
		{
			reset_hook.invoke<void>(dvar, source);
			if (is_float(dvar) && is_scale(dvar->name)
				&& source == game::DVAR_SOURCE_EXTERNAL && current_float(dvar) == dvar->reset.value)
			{
				desired_scale.store(no_scale_override);
			}
		}
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

			set_variant_hook.create(game::Dvar_SetVariant, set_variant_stub);
			reset_hook.create(game::Dvar_Reset, reset_stub);
			get_fov_range_hook.create(game::CG_GetFovRange, get_fov_range_stub);
			if (game::environment::is_singleplayer())
			{
				register_float_hook.create(game::Dvar_RegisterFloat, register_float_stub);

				// SP inlines the upper FOV limit in display adjustment and camera updates.
				// Replace each cmp/cmov pair with mov eax, 160; preserve the calculated minimum.
				for (const auto address : {0x240D5A_g, 0x2426D7_g})
				{
					utils::hook::set<uint8_t>(address, 0xB8);
					utils::hook::set<uint32_t>(address + 1, static_cast<uint32_t>(maximum_fov));
				}
			}
			else
			{
				register_float_hook.create(game::Dvar_RegisterFloatProtected, register_float_stub);
			}
		}
	};
}

REGISTER_COMPONENT(fov::component)
