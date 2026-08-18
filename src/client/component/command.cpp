#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console/console.hpp"

#include <game/game.hpp>

#include <utils/hook.hpp>
#include <utils/string.hpp>
#include <utils/memory.hpp>
#include <utils/io.hpp>

namespace command
{
	namespace
	{
		constexpr auto entity_flag_godmode = 0x1;
		constexpr auto entity_flag_demigod = 0x2;
		constexpr auto entity_flag_notarget = 0x4;
		constexpr auto client_flag_noclip = 0x1;
		constexpr auto client_flag_ufo = 0x2;
		constexpr auto means_of_death_suicide = 13;

		game::dvar_t* sv_cheats = nullptr;

		utils::hook::detour client_command_mp_hook;
		utils::hook::detour client_command_sp_hook;

		std::unordered_map<std::string, command_param_function>& get_command_map()
		{
			static std::unordered_map<std::string, command_param_function> map{};
			return map;
		}

		std::unordered_map<std::string, sv_command_param_function>& get_sv_command_map()
		{
			static std::unordered_map<std::string, sv_command_param_function> map{};
			return map;
		}

		struct mp_player_context
		{
			game::mp::client_t* session{};
			game::mp::gentity_s* entity{};

			explicit operator bool() const
			{
				return this->session && this->entity && this->entity->client;
			}
		};

		game::sp::gentity_s* get_sp_player()
		{
			if (!game::SV_Loaded())
			{
				return nullptr;
			}

			auto* entity = game::sp::g_entities.get();
			return entity && entity->client ? entity : nullptr;
		}

		mp_player_context get_mp_player(const int client_num)
		{
			if (client_num < 0)
			{
				console::info("This command requires a player context.\n");
				return {};
			}

			if (!game::SV_Loaded())
			{
				return {};
			}

			const auto max_clients = *game::sv_maxclients;
			auto* clients = *game::mp::svs_clients;
			auto* entities = game::mp::g_entities.get();
			if (!clients || !entities || client_num >= max_clients)
			{
				return {};
			}

			auto* session = &clients[client_num];
			auto* entity = &entities[client_num];
			if (!entity->client)
			{
				return {};
			}

			return {session, entity};
		}

		void send_sp_game_message(const char* message)
		{
			game::sp::CG_GameMessage(0, message, 0);
		}

		void send_mp_game_message(const mp_player_context& player, const char* message)
		{
			game::SV_SendServerCommand(player.session, game::SV_CMD_RELIABLE, "f \"%s\"", message);
		}

		void send_sp_toggle_message(const char* name, const bool enabled)
		{
			send_sp_game_message(utils::string::va("%s %s", name, enabled ? "^2ON" : "^1OFF"));
		}

		void send_mp_toggle_message(const mp_player_context& player, const char* name, const bool enabled)
		{
			send_mp_game_message(player, utils::string::va("%s %s", name, enabled ? "^2ON" : "^1OFF"));
		}

		bool cheats_ok(const mp_player_context& player)
		{
			if (!sv_cheats || !sv_cheats->current.enabled)
			{
				send_mp_game_message(player, "Cheats are not enabled on this server");
				return false;
			}

			if (player.entity->health <= 0)
			{
				send_mp_game_message(player, "You must be alive to use this command");
				return false;
			}

			return true;
		}

		void toggle_sp_entity_flag(const int flag, const char* name)
		{
			auto* player = get_sp_player();
			if (!player)
			{
				return;
			}

			player->flags ^= flag;
			send_sp_toggle_message(name, (player->flags & flag) != 0);
		}

		void toggle_sp_client_flag(const int flag, const char* name)
		{
			auto* player = get_sp_player();
			if (!player)
			{
				return;
			}

			player->client->flags ^= flag;
			send_sp_toggle_message(name, (player->client->flags & flag) != 0);
		}

		void toggle_mp_entity_flag(const int client_num, const int flag, const char* name)
		{
			const auto player = get_mp_player(client_num);
			if (!player || !cheats_ok(player))
			{
				return;
			}

			player.entity->flags ^= flag;
			send_mp_toggle_message(player, name, (player.entity->flags & flag) != 0);
		}

		void toggle_mp_client_flag(const int client_num, const int flag, const char* name)
		{
			const auto player = get_mp_player(client_num);
			if (!player || !cheats_ok(player))
			{
				return;
			}

			player.entity->client->flags ^= flag;
			send_mp_toggle_message(player, name, (player.entity->client->flags & flag) != 0);
		}

		void give_sp(const params& params)
		{
			if (!get_sp_player())
			{
				return;
			}

			if (params.size() < 2 || !params[1][0])
			{
				send_sp_game_message("You did not specify a weapon name");
				return;
			}

			auto* ps = game::sp::SV_GetPlayerstateForClientNum(0);
			if (!ps)
			{
				return;
			}

			const auto weapon = game::sp::G_GetWeaponForName(params[1]);
			if (!weapon.weaponIdx)
			{
				send_sp_game_message("Weapon not found");
				return;
			}

			if (!game::sp::G_GivePlayerWeapon(ps, &weapon, 0, 0, 0, 0))
			{
				send_sp_game_message("Unable to give weapon");
				return;
			}

			game::sp::G_InitializeAmmo(ps, &weapon, 0);
			game::sp::G_SelectWeapon(0, &weapon);
		}

		void take_sp(const params& params)
		{
			if (!get_sp_player())
			{
				return;
			}

			if (params.size() < 2 || !params[1][0])
			{
				send_sp_game_message("You did not specify a weapon name");
				return;
			}

			auto* ps = game::sp::SV_GetPlayerstateForClientNum(0);
			if (!ps)
			{
				return;
			}

			const auto weapon = game::sp::G_GetWeaponForName(params[1]);
			if (!weapon.weaponIdx)
			{
				send_sp_game_message("Weapon not found");
				return;
			}

			game::sp::G_TakePlayerWeapon(ps, &weapon);
		}

		void kill_sp()
		{
			auto* player = get_sp_player();
			if (!player || player->health <= 0)
			{
				return;
			}

			player->flags &= ~(entity_flag_godmode | entity_flag_demigod);
			player->client->flags &= ~(client_flag_noclip | client_flag_ufo);

			const game::Weapon weapon{};
			game::sp::G_Damage(player, player, player, nullptr, nullptr, 100000, 5,
				means_of_death_suicide, &weapon, false, 0, 0, 0, game::scr_string_t_dummy);
		}

		void give_mp(const int client_num, const params_sv& params)
		{
			const auto player = get_mp_player(client_num);
			if (!player || !cheats_ok(player))
			{
				return;
			}

			if (params.size() < 2 || !params[1][0])
			{
				send_mp_game_message(player, "You did not specify a weapon name");
				return;
			}

			auto* ps = game::mp::SV_GetPlayerstateForClientNum(client_num);
			if (!ps)
			{
				return;
			}

			const auto weapon = game::mp::G_GetWeaponForName(params[1]);
			if (!weapon.weaponIdx)
			{
				send_mp_game_message(player, "Weapon not found");
				return;
			}

			if (!game::mp::G_GivePlayerWeapon(ps, &weapon, 0, 0, 0, 0, 0, 0))
			{
				send_mp_game_message(player, "Unable to give weapon");
				return;
			}

			game::mp::G_InitializeAmmo(ps, &weapon, 0);
			game::mp::G_SelectWeapon(static_cast<char>(client_num), &weapon);
		}

		void take_mp(const int client_num, const params_sv& params)
		{
			const auto player = get_mp_player(client_num);
			if (!player || !cheats_ok(player))
			{
				return;
			}

			if (params.size() < 2 || !params[1][0])
			{
				send_mp_game_message(player, "You did not specify a weapon name");
				return;
			}

			auto* ps = game::mp::SV_GetPlayerstateForClientNum(client_num);
			if (!ps)
			{
				return;
			}

			const auto weapon = game::mp::G_GetWeaponForName(params[1]);
			if (!weapon.weaponIdx)
			{
				send_mp_game_message(player, "Weapon not found");
				return;
			}

			game::mp::G_TakePlayerWeapon(ps, &weapon);
		}

		void kill_mp(const int client_num)
		{
			const auto player = get_mp_player(client_num);
			if (!player || !cheats_ok(player))
			{
				return;
			}

			game::mp::PlayerCmd_Suicide({static_cast<unsigned short>(client_num), 0});
		}

		struct asset_list_context
		{
			game::XAssetType type{};
			std::string filter{};
			std::vector<std::string> names{};
		};

		void collect_asset_name(const game::XAssetHeader header, void* data)
		{
			auto& context = *static_cast<asset_list_context*>(data);
			const game::XAsset asset{context.type, header};
			const auto* asset_name = game::DB_GetXAssetName(&asset);

			if (!asset_name || (!context.filter.empty()
				&& !utils::string::match_compare(context.filter, asset_name, false)))
			{
				return;
			}

			context.names.emplace_back(asset_name);
		}

		void list_asset_pool(const params& arguments)
		{
			if (arguments.size() < 2)
			{
				console::info("listassetpool <poolnumber> [filter]: list all the assets in the specified pool\n");

				for (auto i = 0; i < game::ASSET_TYPE_COUNT; ++i)
				{
					console::info("%d %s\n", i,
						game::DB_GetXAssetTypeName(static_cast<game::XAssetType>(i)));
				}

				return;
			}

			const auto type_index = std::atoi(arguments[1]);
			if (type_index < 0 || type_index >= game::ASSET_TYPE_COUNT)
			{
				console::error("Invalid pool passed must be between [%d, %d]\n",
					0, game::ASSET_TYPE_COUNT - 1);
				return;
			}

			const auto type = static_cast<game::XAssetType>(type_index);
			console::info("Listing assets in pool %s\n", game::DB_GetXAssetTypeName(type));

			asset_list_context context{type, arguments[2]};
			game::DB_EnumXAssets_FastFile(type, collect_asset_name, &context, true);

			for (const auto& name : context.names)
			{
				console::info("%s\n", name.data());
			}
		}

		void dump_commands(const params& arguments)
		{
			console::info("================================ COMMAND DUMP =====================================\n");

			std::string filename{};
			if (arguments.size() == 2)
			{
				filename = "s2x/";
				filename.append(arguments[1]);

				if (!filename.ends_with(".txt"))
				{
					filename.append(".txt");
				}
			}

			auto* command = *game::cmd_functions;
			auto command_count = 0;

			while (command)
			{
				if (command->name)
				{
					if (!filename.empty())
					{
						const auto line = std::format("{}\r\n", command->name);
						utils::io::write_file(filename, line, command_count != 0);
					}

					console::info("%s\n", command->name);
					++command_count;
				}

				command = command->next;
			}

			console::info("\n%i commands\n", command_count);
			console::info("================================ END COMMAND DUMP =================================\n");
		}

		void add_utility_commands()
		{
			command::add("listassetpool", list_asset_pool);
			command::add("commandDump", dump_commands);
		}

		void add_sp_developer_commands()
		{
			command::add("god", []() { toggle_sp_entity_flag(entity_flag_godmode, "godmode"); });
			command::add("demigod", []() { toggle_sp_entity_flag(entity_flag_demigod, "demigod mode"); });
			command::add("notarget", []() { toggle_sp_entity_flag(entity_flag_notarget, "notarget"); });
			command::add("noclip", []() { toggle_sp_client_flag(client_flag_noclip, "noclip"); });
			command::add("ufo", []() { toggle_sp_client_flag(client_flag_ufo, "ufo"); });
			command::add("give", give_sp);
			command::add("take", take_sp);
			command::add("kill", kill_sp);
		}

		void add_mp_developer_commands()
		{
			command::add_sv("god", [](const int client_num, const params_sv&)
			{
				toggle_mp_entity_flag(client_num, entity_flag_godmode, "godmode");
			});
			command::add_sv("demigod", [](const int client_num, const params_sv&)
			{
				toggle_mp_entity_flag(client_num, entity_flag_demigod, "demigod mode");
			});
			command::add_sv("notarget", [](const int client_num, const params_sv&)
			{
				toggle_mp_entity_flag(client_num, entity_flag_notarget, "notarget");
			});
			command::add_sv("noclip", [](const int client_num, const params_sv&)
			{
				toggle_mp_client_flag(client_num, client_flag_noclip, "noclip");
			});
			command::add_sv("ufo", [](const int client_num, const params_sv&)
			{
				toggle_mp_client_flag(client_num, client_flag_ufo, "ufo");
			});
			command::add_sv("give", give_mp);
			command::add_sv("take", take_mp);
			command::add_sv("kill", [](const int client_num, const params_sv&) { kill_mp(client_num); });
		}

		void execute_custom_command()
		{
			const params params{};
			const auto command = utils::string::to_lower(params[0]);

			auto& map = get_command_map();
			const auto entry = map.find(command);

			if (entry != map.end())
			{
				entry->second(params);
			}
		}

		bool execute_custom_sv_command_internal(const int client_num, const params_sv& params)
		{
			const auto command = utils::string::to_lower(params[0]);

			auto& map = get_sv_command_map();
			const auto entry = map.find(command);

			if (entry == map.end())
			{
				return false;
			}

			entry->second(client_num, params);
			return true;
		}

		void execute_custom_sv_command()
		{
			const params_sv params{};

			execute_custom_sv_command_internal(-1, params);
		}

		void forward_custom_sv_command()
		{
			const params params{};
			const auto text = params.join(0);
			const auto local_client_num = game::cmd_args->localClientNum[game::cmd_args->nesting];
			game::CL_ForwardCommandToServer(local_client_num, text.data());
		}

		void client_command_mp_stub(const int client_num)
		{
			const params_sv params{};

			const auto handled = execute_custom_sv_command_internal(client_num, params);
			if (!handled)
			{
				client_command_mp_hook.invoke<void>(client_num);
			}
		}

		void client_command_sp_stub(const int client_num, const char* text)
		{
			params_sv params{ text };

			const auto handled = execute_custom_sv_command_internal(client_num, params);
			if (!handled)
			{
				client_command_sp_hook.invoke<void>(client_num, text);
			}
		}

		void add_raw(const char* name, void(*callback)())
		{
			auto& allocator = *utils::memory::get_allocator();

			const auto* command_name = allocator.duplicate_string(name);
			auto* cmd_function = allocator.allocate<game::cmd_function_s>();

			game::Cmd_AddCommandInternal(command_name, callback, cmd_function);
		}

		void add_server_raw(const char* name)
		{
			auto& allocator = *utils::memory::get_allocator();

			const auto* command_name = allocator.duplicate_string(name);
			auto* local_cmd_function = allocator.allocate<game::cmd_function_s>();
			auto* server_cmd_function = allocator.allocate<game::cmd_function_s>();

			const auto local_callback = game::environment::is_dedicated()
				? game::Cbuf_AddServerText_f.get()
				: forward_custom_sv_command;

			game::Cmd_AddCommandInternal(command_name, local_callback, local_cmd_function);
			game::Cmd_AddServerCommandInternal(command_name, execute_custom_sv_command, server_cmd_function);
		}
	}

	params::params()
		: nesting_(game::cmd_args->nesting)
	{
		assert(this->nesting_ < game::CMD_MAX_NESTING);
	}

	params::params(const std::string& text)
		: needs_end_(true)
	{
		game::Cmd_TokenizeString(text.data());
		this->nesting_ = game::cmd_args->nesting;

		assert(this->nesting_ < game::CMD_MAX_NESTING);
	}

	params::~params()
	{
		if (this->needs_end_)
		{
			game::Cmd_EndTokenizedString();
		}
	}

	int params::size() const
	{
		return game::cmd_args->argc[this->nesting_];
	}

	const char* params::get(const int index) const
	{
		if (index < 0 || index >= this->size())
		{
			return "";
		}

		return game::cmd_args->argv[this->nesting_][index];
	}

	std::string params::join(const int index) const
	{
		std::string result{};

		for (auto i = index; i < this->size(); ++i)
		{
			if (i > index)
			{
				result.append(" ");
			}

			result.append(this->get(i));
		}

		return result;
	}

	std::vector<std::string> params::get_all() const
	{
		std::vector<std::string> result{};

		for (auto i = 0; i < this->size(); ++i)
		{
			result.emplace_back(this->get(i));
		}

		return result;
	}

	params_sv::params_sv()
		: nesting_(game::sv_cmd_args->nesting)
	{
		assert(this->nesting_ < game::CMD_MAX_NESTING);
	}

	params_sv::params_sv(const std::string& text)
		: needs_end_(true)
	{
		game::SV_Cmd_TokenizeString(text.data());
		this->nesting_ = game::sv_cmd_args->nesting;

		assert(this->nesting_ < game::CMD_MAX_NESTING);
	}

	params_sv::~params_sv()
	{
		if (this->needs_end_)
		{
			game::SV_Cmd_EndTokenizedString();
		}
	}

	int params_sv::size() const
	{
		return game::sv_cmd_args->argc[this->nesting_];
	}

	const char* params_sv::get(const int index) const
	{
		if (index < 0 || index >= this->size())
		{
			return "";
		}

		return game::sv_cmd_args->argv[this->nesting_][index];
	}

	std::string params_sv::join(const int index) const
	{
		std::string result{};

		for (auto i = index; i < this->size(); ++i)
		{
			if (i > index)
			{
				result.append(" ");
			}

			result.append(this->get(i));
		}

		return result;
	}

	std::vector<std::string> params_sv::get_all() const
	{
		std::vector<std::string> result{};

		for (auto i = 0; i < this->size(); ++i)
		{
			result.emplace_back(this->get(i));
		}

		return result;
	}

	void add(const std::string& command, command_function function)
	{
		add(command, [function = std::move(function)](const params&)
		{
			function();
		});
	}

	void add(const std::string& command, command_param_function function)
	{
		const auto lower_command = utils::string::to_lower(command);

		auto& map = get_command_map();
		const auto already_registered = map.contains(lower_command);

		map[lower_command] = std::move(function);

		if (already_registered)
		{
			return;
		}

		add_raw(command.data(), execute_custom_command);
	}

	void add_sv(const std::string& command, sv_command_param_function function)
	{
		const auto lower_command = utils::string::to_lower(command);

		auto& map = get_sv_command_map();
		const auto already_registered = map.contains(lower_command);

		map[lower_command] = std::move(function);

		if (already_registered)
		{
			return;
		}

		add_server_raw(command.data());
	}

	struct component final : generic_component
	{
		void post_unpack() override
		{
			if (game::environment::uses_multiplayer_binary())
			{
				client_command_mp_hook.create(0x54EE80_g, client_command_mp_stub);
				sv_cheats = game::Dvar_RegisterBool("sv_cheats", false, game::DVAR_FLAG_REPLICATED);
				add_mp_developer_commands();
			}
			else
			{
				client_command_sp_hook.create(0x366270_g, client_command_sp_stub);
				add_sp_developer_commands();
			}

			add_utility_commands();

			command::add("quit", []()
			{
				game::Com_Quit_f();
			});
		}
	};
}

REGISTER_COMPONENT(command::component)
