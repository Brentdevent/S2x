#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "master_server.hpp"
#include "network.hpp"
#include "party.hpp"
#include "scheduler.hpp"

#include "console/console.hpp"

#include "game/game.hpp"
#include "game/ui_scripting/execution.hpp"

#include "ui_scripting.hpp"

#include <utils/cryptography.hpp>
#include <utils/info_string.hpp>
#include <utils/string.hpp>

#include <algorithm>
#include <charconv>

namespace server_list
{
	namespace
	{
		constexpr auto server_timeout = 10s;
		constexpr auto master_timeout = 5s;
		constexpr auto server_limit = 128ull;
		constexpr auto query_limit = 3ull;

		struct server_info
		{
			game::netadr_s address{};
			std::string address_string{};
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

		bool parse_info_int(const std::string& value, const int minimum,
			const int maximum, int& result)
		{
			if (value.empty())
			{
				return false;
			}

			int parsed{};
			const auto [end, error] = std::from_chars(
				value.data(), value.data() + value.size(), parsed);
			if (error != std::errc{} || end != value.data() + value.size()
				|| parsed < minimum || parsed > maximum)
			{
				return false;
			}

			result = parsed;
			return true;
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

		bool parse_getservers_response(const std::string_view& data, std::vector<game::netadr_s>& addresses,
			bool& saw_eot)
		{
			saw_eot = false;

			for (auto index = 0ull; index < data.size();)
			{
				if (data[index] != '\\')
				{
					return false;
				}

				if (index + 4 <= data.size() && data.compare(index + 1, 3, "EOT") == 0)
				{
					saw_eot = true;
					return true;
				}

				if (index + 7 > data.size())
				{
					return false;
				}

				game::netadr_s address{};
				address.type = game::NA_IP;
				address.localNetID = game::NS_SERVER;
				address.addrHandleIndex = 0;
				std::memcpy(address.ip, data.data() + index + 1, 4);
				std::memcpy(&address.port, data.data() + index + 5, 2);

				if (address.port && addresses.size() < server_limit)
				{
					addresses.emplace_back(address);
				}

				index += 7;
			}

			return true;
		}

		void handle_getservers_response(const game::netadr_s& from, const std::string_view& data)
		{
			{
				std::lock_guard<std::mutex> _{mutex};
				if (!master_state.requesting || master_state.address != from)
				{
					return;
				}
			}

			std::vector<game::netadr_s> addresses;
			bool saw_eot{};
			if (!parse_getservers_response(data, addresses, saw_eot))
			{
				console::warn("[server_list] ignored malformed response from master\n");
				return;
			}

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

						queries.emplace_back(entry->first, queued.challenge);
					}

					++entry;
				}
			}

			for (const auto& [address, challenge] : queries)
			{
				network::send(address, "s2x_getInfo", challenge);
			}
		}

		void refresh_server_list()
		{
			game::netadr_s master{};
			if (!master_server::get_address(master))
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
			network::send(master, "getservers", utils::string::va("%s %i", master_server::game_name, PROTOCOL));
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

			party::queue_connect(std::move(address));
		}

		void handle_info_response(const game::netadr_s& from, const std::string_view& data)
		{
			auto address = from;
			if (address.type == game::NA_BROADCAST)
			{
				address.type = game::NA_IP;
				address.localNetID = game::NS_SERVER;
				address.addrHandleIndex = 0;
			}

			const utils::info_string info{data};
			const auto challenge = info.get("challenge");

			std::chrono::steady_clock::time_point query_start{};

			{
				std::lock_guard<std::mutex> _{mutex};

				const auto queued = master_state.queued_servers.find(address);
				if (queued == master_state.queued_servers.end() || !queued->second.queried ||
					queued->second.challenge != challenge)
				{
					return;
				}

				query_start = queued->second.query_start;
			}

			if (info.get("gamename") != master_server::game_name || info.get("s2x") != "1")
			{
				drop_server(address);
				return;
			}

			const auto& mode = game::environment::get_online_mode_info();
			if (info.get("mode") != std::string{mode.token})
			{
				drop_server(address);
				return;
			}

			int protocol{};
			if (!parse_info_int(info.get("protocol"), 0, std::numeric_limits<int>::max(), protocol)
				|| protocol != PROTOCOL)
			{
				drop_server(address);
				return;
			}

			int server_running{};
			if (!parse_info_int(info.get("sv_running"), 0, 1, server_running))
			{
				drop_server(address);
				return;
			}

			auto party_session = 0;
			const auto party_session_value = info.get("party_session");
			if (!party_session_value.empty()
				&& !parse_info_int(party_session_value, 0, 1, party_session))
			{
				drop_server(address);
				return;
			}

			if (!server_running && !party_session)
			{
				drop_server(address);
				return;
			}

			const auto hostname = get_info_value(info, "hostname", "sv_hostname");
			const auto mapname = info.get("mapname");
			const auto gametype = info.get("gametype");
			int clients{};
			int bots{};
			int max_clients{};
			const auto max_clients_value = get_info_value(info, "sv_maxclients", "maxclients");
			if (!parse_info_int(info.get("clients"), 0, mode.max_players, clients)
				|| !parse_info_int(info.get("bots"), 0, mode.max_players, bots)
				|| !parse_info_int(max_clients_value, 1, mode.max_players, max_clients)
				|| clients > max_clients || bots > clients)
			{
				drop_server(address);
				return;
			}
			const auto ping = std::min(
				static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - query_start).count()),
				999
			);

			{
				std::lock_guard<std::mutex> _{mutex};

				auto* server = get_server_by_address(address);
				if (!server)
				{
					return;
				}

				master_state.queued_servers.erase(address);

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

			lobby["CancelS2xConnection"] = []
			{
				party::cancel_pending_connection();
			};

			console::info("[server_list] installed Lobby server browser functions\n");
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

			ui_scripting::on_start(install_lobby_functions);

			network::on("getserversResponse", [](const game::netadr_s& from, const std::string_view& data)
			{
				handle_getservers_response(from, data);
			});

			network::on("s2x_infoResponse", [](const game::netadr_s& from, const std::string_view& data)
			{
				handle_info_response(from, data);
			});

			scheduler::loop(do_frame_work, scheduler::pipeline::main, 50ms);
		}
	};
}

REGISTER_COMPONENT(server_list::component)
