#include <std_include.hpp>
#include "loader/component_loader.hpp"

#ifdef DEBUG
#include "scheduler.hpp"
#include "game/game.hpp"

#include <utils/string.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace debug_draw
{
	namespace
	{
		enum model_draw_e : int
		{
			off,
			static_models,
			dynent_models,
			scene_models,
			all,
		};

		const char* model_draw_values[] =
		{
			"off",
			"static models",
			"dynent models",
			"scene models",
			"all",
			nullptr,
		};

		const game::dvar_t* r_draw_model_names{};
		const game::dvar_t* r_draw_fx_info{};
		const game::dvar_t* r_draw_light_origins{};
		const game::dvar_t* r_draw_debug_distance{};

		constexpr float static_model_color[4] = {1.0f, 0.0f, 1.0f, 1.0f};
		constexpr float dynent_model_color[4] = {0.0f, 0.0f, 1.0f, 1.0f};
		constexpr float scene_model_color[4] = {1.0f, 1.0f, 0.0f, 1.0f};
		constexpr float fx_color[4] = {1.0f, 0.0f, 0.0f, 1.0f};
		// Fallback for lights without a usable color.
		constexpr float light_color[4] = {0.5f, 0.5f, 0.5f, 1.0f};

		// MP 0xE63A0 clamps the scene model count to this size.
		constexpr int max_scene_models = 0x600;
		// MP 0x516970 masks effect indices into allEffectHandles with 0x7FF.
		constexpr int effect_handle_mask = 0x7FF;

		struct draw_context
		{
			game::Font_s* font{};
			const game::ScreenPlacement* placement{};
			const void* style{};
			game::vec3_t view_origin{};
			float max_distance_sq{};
		};

		struct fx_label
		{
			const char* name;
			game::vec3_t origin;
		};

		bool in_range(const draw_context& context, const float* origin)
		{
			const auto x = origin[0] - context.view_origin[0];
			const auto y = origin[1] - context.view_origin[1];
			const auto z = origin[2] - context.view_origin[2];
			return x * x + y * y + z * z <= context.max_distance_sq;
		}

		void draw_label(const draw_context& context, const char* text, const float* origin, const float* color)
		{
			if (!text || !in_range(context, origin))
			{
				return;
			}

			game::vec2_t screen{};
			if (!game::CG_WorldPosToScreenPosReal(game::LOCAL_CLIENT_0, context.placement, origin, screen))
			{
				return;
			}

			// Prepare the glow glyphs before the backend font pass, not during text drawing.
			const auto width = game::R_TextWidthWithFlags(text, 0, context.font, 0, 0,
				context.font->pixelHeight, 0.0f, 1);
			game::R_AddCmdDrawText(text, std::numeric_limits<int>::max(), context.font, 0, 0,
				context.font->pixelHeight, screen[0] - width * 0.5f, screen[1], 1.0f, 1.0f, 0.0f, color,
				context.style);
		}

		void draw_static_models(const draw_context& context)
		{
			const auto* world = *game::mp::gfx_world;
			if (!world || !world->models)
			{
				return;
			}

			for (unsigned int i = 0; i < world->model_count; ++i)
			{
				const auto& inst = world->models[i];
				if (inst.model)
				{
					draw_label(context, inst.model->name, inst.origin, static_model_color);
				}
			}
		}

		void draw_dynent_models(const draw_context& context)
		{
			const auto& cm = *game::mp::cm;
			const auto* poses = cm.dynEntPoseList[0];
			const auto* clients = cm.dynEntClientList[0];
			if (!poses || !clients)
			{
				return;
			}

			for (unsigned short i = 0; i < cm.dynEntCount[0]; ++i)
			{
				if (clients[i].activeModel)
				{
					draw_label(context, clients[i].activeModel->name, poses[i].origin, dynent_model_color);
				}
			}
		}

		void draw_scene_models(const draw_context& context)
		{
			const auto count = std::clamp(*game::mp::scene_model_count, 0, max_scene_models);
			const auto* models = game::mp::scene_models.get();
			for (auto i = 0; i < count; ++i)
			{
				if (models[i].model)
				{
					draw_label(context, models[i].model->name, models[i].origin, scene_model_color);
				}
			}
		}

		void draw_light_origins(const draw_context& context)
		{
			const auto* com_world = game::mp::com_world.get();
			if (!com_world->primaryLights)
			{
				return;
			}

			for (unsigned int i = 0; i < com_world->primaryLightCount; ++i)
			{
				const auto& light = com_world->primaryLights[i];
				if (!in_range(context, light.origin))
				{
					continue;
				}

				// Scale the brightest channel to 1 so HDR and dim lights stay readable.
				float color[4] = {light_color[0], light_color[1], light_color[2], light_color[3]};
				const auto max_channel = std::max({light.color[0], light.color[1], light.color[2]});
				if (std::isfinite(max_channel) && max_channel > 0.0f)
				{
					for (auto c = 0; c < 3; ++c)
					{
						color[c] = std::max(light.color[c], 0.0f) / max_channel;
					}
				}

				draw_label(context, utils::string::va("%.1f, %.1f, %.1f (%u)", light.origin[0], light.origin[1],
					light.origin[2], i), light.origin, color);
			}
		}

		void draw_fx(const draw_context& context)
		{
			auto* system = game::FX_GetSystem(game::LOCAL_CLIENT_0);
			if (!system || !system->lock || !system->effects || !system->allEffectHandles)
			{
				return;
			}

			// Copy under the read lock and draw after releasing it.
			std::vector<fx_label> labels{};
			game::FX_WaitEnterReadSystemLock(system->lock);

			for (auto i = system->firstActiveEffect; i != system->firstFreeEffect; ++i)
			{
				const auto handle = system->allEffectHandles[i & effect_handle_mask];
				const auto* effect = reinterpret_cast<const game::FxEffect*>(
					reinterpret_cast<const char*>(system->effects) + (static_cast<size_t>(handle) << 4));
				if (!effect->def || !effect->def->name || !in_range(context, effect->frameNow.origin))
				{
					continue;
				}

				auto& label = labels.emplace_back();
				label.name = effect->def->name;
				std::copy_n(effect->frameNow.origin, 3, label.origin);
			}

			game::FX_ExitReadSystemLock(system->lock);

			for (const auto& label : labels)
			{
				draw_label(context, label.name, label.origin, fx_color);
			}
		}

		void draw()
		{
			const auto model_mode = r_draw_model_names->current.integer;
			const auto fx_enabled = r_draw_fx_info->current.enabled;
			const auto lights_enabled = r_draw_light_origins->current.enabled;
			if (model_mode == off && !fx_enabled && !lights_enabled)
			{
				return;
			}

			if (game::virtual_lobby_loaded() || !game::CL_IsLocalClientInGame(game::LOCAL_CLIENT_0))
			{
				return;
			}

			draw_context context{};
			context.font = game::R_RegisterFont("fonts/fira_mono_regular.ttf", 16);
			context.placement = game::ScrPlace_GetViewPlacement(game::LOCAL_CLIENT_0);
			if (!context.font || !context.placement)
			{
				return;
			}

			context.style = game::R_Font_GetLegacyFontStyle(6);
			// This native wrapper calls the protected CG_GetLocalClient from inside S2.
			game::CL_GetViewPos(game::LOCAL_CLIENT_0, context.view_origin);

			const auto distance = static_cast<float>(r_draw_debug_distance->current.integer);
			context.max_distance_sq = distance * distance;

			if (model_mode == static_models || model_mode == all)
			{
				draw_static_models(context);
			}
			if (model_mode == dynent_models || model_mode == all)
			{
				draw_dynent_models(context);
			}
			if (model_mode == scene_models || model_mode == all)
			{
				draw_scene_models(context);
			}
			if (fx_enabled)
			{
				draw_fx(context);
			}
			if (lights_enabled)
			{
				draw_light_origins(context);
			}
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated())
			{
				return;
			}

			r_draw_model_names = game::Dvar_RegisterEnum("r_drawModelNames", model_draw_values, off,
				game::DVAR_FLAG_CHEAT);
			r_draw_fx_info = game::Dvar_RegisterBool("r_drawFxInfo", false, game::DVAR_FLAG_CHEAT);
			r_draw_light_origins = game::Dvar_RegisterBool("r_drawLightOrigins", false, game::DVAR_FLAG_CHEAT);
			r_draw_debug_distance = game::Dvar_RegisterInt("r_drawDebugDistance", 1000, 0, 50000,
				game::DVAR_FLAG_SAVED);

			scheduler::loop(draw, scheduler::renderer);
		}
	};
}

REGISTER_COMPONENT(debug_draw::component)
#endif
