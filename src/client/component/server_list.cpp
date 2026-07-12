#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "network.hpp"
#include "scheduler.hpp"

#include "console/console.hpp"

#include "game/game.hpp"
#include "game/ui_scripting/execution.hpp"

#include "ui_scripting.hpp"

#include <utils/cryptography.hpp>
#include <utils/info_string.hpp>
#include <utils/string.hpp>

#include <algorithm>

namespace server_list
{
	namespace
	{
		constexpr auto default_master_server_name = "server.master.dev";
		constexpr auto default_master_server_port = 20810;
		constexpr auto master_game = "S2";
		constexpr auto server_timeout = 10s;
		constexpr auto master_timeout = 5s;
		constexpr auto heartbeat_interval = 2min;
		constexpr auto server_limit = 128ull;
		constexpr auto query_limit = 3ull;

		struct server_info
		{
			game::netadr_s address{};
			std::string address_string{};
			std::string challenge{};
			std::chrono::steady_clock::time_point query_start{};
			bool queried{};
			bool valid{};

			std::string hostname{};
			std::string mapname{};
			std::string gametype{};
			int clients{};
			int max_clients{};
			int ping{};
			std::string status{};
		};

		struct queued_server
		{
			std::string challenge{};
			std::chrono::steady_clock::time_point query_start{};
			bool queried{};
		};

		struct master_state_t
		{
			game::netadr_s address{};
			bool requesting{};
			std::chrono::steady_clock::time_point request_start{};
			std::unordered_map<game::netadr_s, queued_server> queued_servers{};
		};

		std::mutex mutex;
		std::vector<server_info> servers;
		master_state_t master_state;

		game::dvar_t* master_server_name;
		game::dvar_t* master_server_port;

		std::string get_dvar_string(const game::dvar_t* dvar)
		{
			if (!dvar || !dvar->current.string)
			{
				return {};
			}

			return dvar->current.string;
		}

		int get_dvar_int(const game::dvar_t* dvar, const int fallback)
		{
			if (!dvar)
			{
				return fallback;
			}

			return dvar->current.integer;
		}

		int parse_int(const std::string& value, const int fallback = 0)
		{
			if (value.empty())
			{
				return fallback;
			}

			return std::atoi(value.data());
		}

		std::string get_info_value(const utils::info_string& info, const std::string& key, const std::string& fallback_key = {})
		{
			auto value = info.get(key);
			if (value.empty() && !fallback_key.empty())
			{
				value = info.get(fallback_key);
			}

			return value;
		}

		bool get_master_server(game::netadr_s& address)
		{
			auto name = get_dvar_string(master_server_name);
			utils::string::trim(name);

			if (name.empty())
			{
				name = default_master_server_name;
			}

			auto port = get_dvar_int(master_server_port, default_master_server_port);
			if (port <= 0 || port > 0xFFFF)
			{
				port = default_master_server_port;
			}

			const auto address_string = utils::string::va("%s:%i", name.data(), port);
			if (!game::NET_StringToAdr(address_string, &address))
			{
				console::warn("[server_list] failed to resolve master server '%s'\n", address_string);
				return false;
			}

			if (address.type <= game::NA_BAD)
			{
				console::warn("[server_list] ignoring bad master server address '%s'\n", address_string);
				return false;
			}

			address.localNetID = game::NS_SERVER;
			address.addrHandleIndex = 0;
			return true;
		}

		void sort_servers()
		{
			std::ranges::stable_sort(servers, [](const server_info& a, const server_info& b)
			{
				if (a.valid != b.valid)
				{
					return a.valid > b.valid;
				}

				if (a.clients != b.clients)
				{
					return a.clients > b.clients;
				}

				return a.ping < b.ping;
			});
		}

		server_info* get_server_by_address(const game::netadr_s& address)
		{
			const auto entry = std::ranges::find_if(servers, [&address](const server_info& server)
			{
				return server.address == address;
			});

			return entry == servers.end() ? nullptr : &*entry;
		}

		const server_info* get_display_server(const int index)
		{
			if (index < 0)
			{
				return nullptr;
			}

			auto current_index = 0;
			for (const auto& server : servers)
			{
				if (!server.valid)
				{
					continue;
				}

				if (current_index++ == index)
				{
					return &server;
				}
			}

			return nullptr;
		}

		void remove_server_locked(const game::netadr_s& address)
		{
			std::erase_if(servers, [&address](const server_info& server)
			{
				return server.address == address;
			});
		}

		void drop_server(const game::netadr_s& address)
		{
			std::lock_guard<std::mutex> _{mutex};

			master_state.queued_servers.erase(address);
			remove_server_locked(address);
		}

		bool queue_server_locked(const game::netadr_s& address)
		{
			if (servers.size() >= server_limit)
			{
				return false;
			}

			if (get_server_by_address(address) || master_state.queued_servers.contains(address))
			{
				return false;
			}

			server_info server{};
			server.address = address;
			server.address_string = network::net_adr_to_string(address);
			server.hostname = server.address_string;
			server.status = "Querying";

			servers.emplace_back(std::move(server));
			master_state.queued_servers.emplace(address, queued_server{});
			return true;
		}

		bool parse_getservers_response(const std::string_view& data, std::vector<game::netadr_s>& addresses)
		{
			auto saw_eot = false;

			for (auto index = 0ull; index < data.size();)
			{
				const auto marker = data.find('\\', index);
				if (marker == std::string_view::npos)
				{
					break;
				}

				if (marker + 7 > data.size())
				{
					break;
				}

				if (data.compare(marker + 1, 3, "EOT") == 0)
				{
					saw_eot = true;
					break;
				}

				game::netadr_s address{};
				address.type = game::NA_IP;
				address.localNetID = game::NS_SERVER;
				address.addrHandleIndex = 0;
				std::memcpy(address.ip, data.data() + marker + 1, 4);
				std::memcpy(&address.port, data.data() + marker + 5, 2);

				if (address.port)
				{
					addresses.emplace_back(address);
				}

				index = marker + 7;
			}

			return saw_eot;
		}

		void handle_getservers_response(const game::netadr_s& from, const std::string_view& data)
		{
			std::vector<game::netadr_s> addresses;
			const auto saw_eot = parse_getservers_response(data, addresses);

			auto queued_count = 0ull;

			{
				std::lock_guard<std::mutex> _{mutex};

				if (!master_state.requesting || master_state.address != from)
				{
					return;
				}

				if (saw_eot)
				{
					master_state.requesting = false;
				}

				for (const auto& address : addresses)
				{
					if (queue_server_locked(address))
					{
						++queued_count;
					}
				}
			}

			if (queued_count)
			{
				console::info("[server_list] queued %zu server(s) from master\n", queued_count);
			}
		}

		void do_frame_work()
		{
			std::vector<std::pair<game::netadr_s, std::string>> queries;
			const auto now = std::chrono::steady_clock::now();

			{
				std::lock_guard<std::mutex> _{mutex};

				if (master_state.requesting && now - master_state.request_start > master_timeout)
				{
					master_state.requesting = false;
					console::warn("[server_list] timed out waiting for master response\n");
				}

				for (auto entry = master_state.queued_servers.begin(); entry != master_state.queued_servers.end();)
				{
					auto& queued = entry->second;

					if (queued.queried && now - queued.query_start > server_timeout)
					{
						remove_server_locked(entry->first);
						entry = master_state.queued_servers.erase(entry);
						continue;
					}

					if (!queued.queried && queries.size() < query_limit)
					{
						queued.challenge = utils::cryptography::random::get_challenge();
						queued.query_start = now;
						queued.queried = true;

						if (auto* server = get_server_by_address(entry->first))
						{
							server->challenge = queued.challenge;
							server->query_start = queued.query_start;
							server->queried = true;
						}

						queries.emplace_back(entry->first, queued.challenge);
					}

					++entry;
				}
			}

			for (const auto& [address, challenge] : queries)
			{
				network::send(address, "getInfo", challenge);
			}
		}

		void refresh_server_list()
		{
			game::netadr_s master{};
			if (!get_master_server(master))
			{
				std::lock_guard<std::mutex> _{mutex};
				servers.clear();
				master_state = {};
				return;
			}

			{
				std::lock_guard<std::mutex> _{mutex};
				servers.clear();
				master_state = {};
				master_state.address = master;
				master_state.requesting = true;
				master_state.request_start = std::chrono::steady_clock::now();
			}

			console::info("[server_list] requesting S2 servers from %s\n", network::net_adr_to_string(master));
			network::send(master, "getservers", utils::string::va("%s %i", master_game, PROTOCOL));
		}

		void update_server_display_list()
		{
			do_frame_work();
		}

		int get_server_count()
		{
			std::lock_guard<std::mutex> _{mutex};

			return static_cast<int>(std::ranges::count_if(servers, [](const server_info& server)
			{
				return server.valid;
			}));
		}

		std::string get_server_data(const int index, const int column)
		{
			std::lock_guard<std::mutex> _{mutex};

			const auto* server = get_display_server(index);
			if (!server)
			{
				return {};
			}

			switch (column)
			{
			case 2:
				return server->hostname;
			case 3:
				return server->mapname;
			case 4:
				return utils::string::va("%d/%d", server->clients, server->max_clients);
			case 5:
				return server->gametype;
			case 7:
				return std::to_string(server->ping);
			case 10:
				return server->status;
			default:
				return {};
			}
		}

		void join_server(const int index)
		{
			std::string address;

			{
				std::lock_guard<std::mutex> _{mutex};

				const auto* server = get_display_server(index);
				if (!server)
				{
					return;
				}

				address = server->address_string;
			}

			console::info("[server_list] joining %s\n", address.data());

			scheduler::once([address]()
			{
				game::Cbuf_AddText(0, utils::string::va("connect %s\n", address.data()));
			}, scheduler::pipeline::main);
		}

		void handle_info_response(const game::netadr_s& from, const std::string_view& data)
		{
			const utils::info_string info{data};
			const auto challenge = info.get("challenge");

			std::chrono::steady_clock::time_point query_start{};

			{
				std::lock_guard<std::mutex> _{mutex};

				const auto queued = master_state.queued_servers.find(from);
				if (queued == master_state.queued_servers.end() || !queued->second.queried ||
					queued->second.challenge != challenge)
				{
					return;
				}

				query_start = queued->second.query_start;
			}

			if (info.get("gamename") != master_game)
			{
				drop_server(from);
				return;
			}

			if (parse_int(info.get("protocol")) != PROTOCOL)
			{
				drop_server(from);
				return;
			}

			if (info.get("sv_running") != "1")
			{
				drop_server(from);
				return;
			}

			const auto hostname = get_info_value(info, "hostname", "sv_hostname");
			const auto mapname = info.get("mapname");
			const auto gametype = info.get("gametype");
			const auto clients = std::max(parse_int(info.get("clients")), 0);
			const auto max_clients = std::max(parse_int(info.get("sv_maxclients"), parse_int(info.get("maxclients"))), 0);
			const auto ping = std::min(
				static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - query_start).count()),
				999
			);

			{
				std::lock_guard<std::mutex> _{mutex};

				auto* server = get_server_by_address(from);
				if (!server)
				{
					return;
				}

				master_state.queued_servers.erase(from);

				server->hostname = hostname.empty() ? server->address_string : hostname;
				server->mapname = mapname;
				server->gametype = gametype;
				server->clients = clients;
				server->max_clients = max_clients;
				server->ping = ping;
				server->status = max_clients > 0 && clients >= max_clients ? "Full" : "Joinable";
				server->valid = true;

				sort_servers();
			}
		}

		void install_lobby_functions()
		{
			const auto lua = ui_scripting::get_globals();
			auto lobby_value = lua.get("Lobby");

			ui_scripting::table lobby{};
			if (lobby_value.is<ui_scripting::table>())
			{
				lobby = lobby_value.as<ui_scripting::table>();
			}
			else
			{
				lua["Lobby"] = lobby;
			}

			lobby["BuildServerList"] = [](int)
			{
				update_server_display_list();
			};

			lobby["RefreshServerList"] = [](int, int)
			{
				refresh_server_list();
			};

			lobby["UpdateServerDisplayList"] = [](int)
			{
				update_server_display_list();
			};

			lobby["GetServerCount"] = [](int)
			{
				return get_server_count();
			};

			lobby["GetServerData"] = [](int, int index, int column)
			{
				return get_server_data(index, column);
			};

			lobby["JoinServer"] = [](int, int index)
			{
				join_server(index);
			};

			console::info("[server_list] installed Lobby server browser functions\n");
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			scheduler::once([]()
			{
				master_server_name = game::Dvar_RegisterString("masterServerName", default_master_server_name, game::DVAR_FLAG_SAVED);
				master_server_port = game::Dvar_RegisterInt("masterServerPort", default_master_server_port, 1, 0xFFFF, game::DVAR_FLAG_SAVED);
			}, scheduler::pipeline::main);

			if (game::environment::is_dedi())
			{
				return;
			}

			ui_scripting::on_start(install_lobby_functions);

			network::on("getserversResponse", [](const game::netadr_s& from, const std::string_view& data)
			{
				handle_getservers_response(from, data);
			});

			/*network::on("infoResponse", [](const game::netadr_s& from, const std::string_view& data)
			{
				handle_info_response(from, data);
			});*/

			network::on("s2x_infoResponse", [](const game::netadr_s& from, const std::string_view& data)
			{
				handle_info_response(from, data);
			});

			scheduler::loop(do_frame_work, scheduler::pipeline::main, 50ms);
		}
	};
}

REGISTER_COMPONENT(server_list::component)
