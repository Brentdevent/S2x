#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "scheduler.hpp"
#include "party.hpp"

#include "game/game.hpp"

#include "console/console.hpp"

#include <utils/hook.hpp>

namespace bots
{
	namespace
	{
		int get_requested_bot_count(const command::params& params)
		{
			if (params.size() <= 1)
			{
				return 1;
			}

			return std::max(1, std::atoi(params[1]));
		}

		int get_planned_bot_count(const int requested_count)
		{
			const auto available_slots = party::get_available_match_slots();
			return std::min(requested_count, available_slots);
		}

		void spawn_bot()
		{
			auto* ent = game::mp::SV_AddBot("", 1);

			if (ent)
			{
				game::mp::SV_SpawnTestClient(ent);
			}
		}

		void spawn_bots(const int count)
		{
			for (int i = 0; i < count; ++i)
			{
				spawn_bot();
			}
		}

		void spawn_bot_command(const command::params& params)
		{
			if (!game::is_server_running())
			{
				return;
			}

			const auto requested_count = get_requested_bot_count(params);
			const auto planned_count = get_planned_bot_count(requested_count);

			if (planned_count <= 0)
			{
				console::warn("Cannot spawn bot: match player limit reached\n");
				return;
			}

			scheduler::once([planned_count]
			{
				spawn_bots(planned_count);
			}, scheduler::server);
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			utils::hook::set(game::BG_BotFastFileEnabled, 0xC301B0);
			utils::hook::set(game::BG_BotsUsingTeamDifficulty, 0xC301B0);
			utils::hook::set(game::BG_BotSystemEnabled, 0xC301B0);
			utils::hook::set(game::BG_AgentSystemEnabled, 0xC301B0);
			
			// Not sure, is LUA related (Might need additional patches since it also checks OnlineGame dvar outside this function)
			utils::hook::set(0x388210_g, 0xC301B0);

			command::add("spawnBot", [](const command::params& params)
			{
				spawn_bot_command(params);
			});
		}
	};
}

REGISTER_COMPONENT(bots::component)
