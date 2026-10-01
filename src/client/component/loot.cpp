#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "component/console/console.hpp"
#include "component/scheduler.hpp"

#include "game/game.hpp"
#include "game/demonware/loot_catalog.hpp"
#include "game/demonware/loot_service.hpp"

namespace loot
{
	namespace
	{
		game::dvar_t* infinite_cod_points{};
		game::dvar_t* daily_cod_points{};
		game::dvar_t* match_cod_points{};

		void update_settings()
		{
			demonware::loot_service::set_settings({
				infinite_cod_points->current.enabled,
				static_cast<std::uint32_t>(daily_cod_points->current.integer),
				static_cast<std::uint32_t>(match_cod_points->current.integer),
			});
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

			infinite_cod_points = game::Dvar_RegisterBool("loot_infiniteCodPoints", false, game::DVAR_FLAG_SAVED);
			daily_cod_points = game::Dvar_RegisterInt("loot_dailyCodPoints", 500, 0, 100000, game::DVAR_FLAG_SAVED);
			match_cod_points = game::Dvar_RegisterInt("loot_matchCodPoints", 50, 0, 100000, game::DVAR_FLAG_SAVED);
			update_settings();
			scheduler::loop(update_settings, scheduler::pipeline::main, 1s);

			scheduler::schedule([]
			{
				if (!demonware::loot_catalog::load())
				{
					return scheduler::cond_continue;
				}

				console::debug("[loot] supply drop catalog loaded\n");
				return scheduler::cond_end;
			}, scheduler::pipeline::main, 1s);
		}
	};
}

REGISTER_COMPONENT(loot::component)
