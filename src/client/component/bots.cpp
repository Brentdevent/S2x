#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "scheduler.hpp"

#include "game/game.hpp"

#include "console/console.hpp"

#include <utils/hook.hpp>

namespace bots
{
	namespace
	{
		constexpr int max_match_players = 18;

		int get_connected_client_count()
		{
			int count = 0;
			auto* clients = *game::mp::svs_clients;

			for (int i = 0; i < *game::mp::sv_maxclients; ++i)
			{
				const auto& client = clients[i];

				if (client.state != 0)
				{
					++count;
				}
			}

			return count;
		}

		int get_available_match_slots()
		{
			return std::max(0, max_match_players - get_connected_client_count());
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			utils::hook::set(game::mp::BG_BotFastFileEnabled, 0xC301B0);
			utils::hook::set(game::mp::BG_BotsUsingTeamDifficulty, 0xC301B0);
			utils::hook::set(game::mp::BG_BotSystemEnabled, 0xC301B0);
			utils::hook::set(game::mp::BG_AgentSystemEnabled, 0xC301B0);
			
			// Not sure, is LUA related (Might need additional patches since it also checks OnlineGame dvar outside this function)
			utils::hook::set(0x388210_g, 0xC301B0);

			command::add("SpawnBot", [](const command::params& params)
			{
				if (!game::SV_Loaded() || *game::mp::virtualLobby_Loaded)
				{
					return;
				}

				int requested_count = 1;
				if (params.size() > 1)
				{
					requested_count = std::atoi(params[1]);
				}

				const auto available_slots = get_available_match_slots();
				const auto planned_count = std::min(requested_count, available_slots);

				if (planned_count <= 0)
				{
					console::warn("Cannot spawn bot: match player limit reached\n");
					return;
				}

				scheduler::once([planned_count]
				{
					for (size_t i = 0; i < planned_count; ++i)
					{
						auto* ent = game::mp::SV_AddBot("", 1);

						if (ent)
						{
							game::mp::SV_SpawnTestClient(ent);
						}
					}
				}, scheduler::server);
			});
		}
	};
}

REGISTER_COMPONENT(bots::component)
