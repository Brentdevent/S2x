#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "scheduler.hpp"
#include "party.hpp"
#include "scripting.hpp"

#include "game/game.hpp"

#include "console/console.hpp"

#include <utils/hook.hpp>
#include <utils/cryptography.hpp>
#include <utils/io.hpp>
#include <utils/nt.hpp>
#include <utils/string.hpp>

#include <algorithm>

namespace bots
{
	namespace
	{
		utils::hook::detour bot_get_random_name_hook;
		std::vector<std::string> bot_names;
		std::size_t bot_name_index{};
		bool bot_names_loaded{};

		void load_bot_names()
		{
			if (bot_names_loaded)
			{
				return;
			}

			bot_names_loaded = true;
			const auto path = std::filesystem::path{ utils::nt::library{}.get_folder() } / "s2x" / "bots.txt";
			std::string contents;
			if (!utils::io::read_file(path.string(), &contents))
			{
				return;
			}

			for (auto& name : utils::string::split(contents, '\n'))
			{
				name = utils::string::replace(name, "\r", "");
				if (!name.empty())
				{
					if (name.size() > 30)
					{
						name.resize(30);
					}
					
					bot_names.emplace_back(std::move(name));
				}
			}

			std::mt19937 random{ utils::cryptography::random::get_integer() };
			std::shuffle(bot_names.begin(), bot_names.end(), random);
		}

		const char* get_custom_bot_name()
		{
			load_bot_names();
			if (bot_name_index >= bot_names.size())
			{
				return nullptr;
			}

			return bot_names[bot_name_index++].c_str();
		}

		const char* get_random_bot_name_stub()
		{
			if (const auto* custom_name = get_custom_bot_name())
			{
				return custom_name;
			}

			return bot_get_random_name_hook.invoke<const char*>();
		}

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

			bot_get_random_name_hook.create(0x6C3600_g, get_random_bot_name_stub);
			
			scripting::on_shutdown([](const int clear_scripts)
			{
				if (!clear_scripts)
				{
					return;
				}

				bot_names.clear();
				bot_name_index = 0;
				bot_names_loaded = false;
			});

			command::add("spawnBot", [](const command::params& params)
			{
				spawn_bot_command(params);
			});
		}
	};
}

REGISTER_COMPONENT(bots::component)
