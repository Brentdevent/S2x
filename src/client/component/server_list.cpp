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
#include <utils/io.hpp>
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
		constexpr auto ping_sample_count = 3;
		constexpr auto ping_sample_interval = 200ms;

		struct server_info
		{
			game::netadr_s address{};
			std::string address_string{};
			bool valid{};

			std::string hostname{};
			std::string mapname{};
			std::string map_reference{};
			std::string gametype{};
			int clients{};
			int bots{};
			int max_clients{};
			int ping{};
			std::string status{};
		};

		struct queued_server
		{
			std::string challenge{};
			std::chrono::steady_clock::time_point query_start{};
			std::chrono::steady_clock::time_point next_query{};
			int samples{};
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
		std::vector<std::string> favorites;
		std::string favorites_mode;

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

		bool parse_favorite_address(const std::string& value, game::netadr_s& address)
		{
			const auto separator = value.find(':');
			if (separator == std::string::npos || separator > 15)
			{
				return false;
			}

			int port{};
			address = {};
			if (!parse_info_int(value.substr(separator + 1), 1, 65535, port)
				|| InetPtonA(AF_INET, value.substr(0, separator).data(), address.ip) != 1)
			{
				return false;
			}

			address.type = game::NA_IP;
			address.localNetID = game::NS_SERVER;
			address.port = htons(static_cast<unsigned short>(port));

			return true;
		}

		std::filesystem::path favorites_path()
		{
			return game::get_appdata_path() / ("server_favorites_" + favorites_mode + ".txt");
		}

		void load_favorites_locked()
		{
			const std::string mode{game::environment::get_online_mode_info().token};
			if (favorites_mode == mode)
			{
				return;
			}

			favorites_mode = mode;
			favorites.clear();

			if (utils::io::file_size(favorites_path().wstring()) > 8192)
			{
				return;
			}

			std::ifstream stream(favorites_path());
			std::string line;

			// Bound the file as well as the number of saved endpoints.
			for (size_t bytes = 0; bytes < 8192 && favorites.size() < server_limit && std::getline(stream, line);)
			{
				bytes += line.size() + 1;
				game::netadr_s address{};
				if (line.size() > 21 || !parse_favorite_address(line, address))
				{
					continue;
				}

				const std::string canonical = network::net_adr_to_string(address);
				if (std::ranges::find(favorites, canonical) == favorites.end())
				{
					favorites.emplace_back(canonical);
				}
			}
		}

		bool save_favorites_locked(const std::vector<std::string>& updated)
		{
			const auto path = favorites_path();
			std::error_code error;
			std::filesystem::create_directories(path.parent_path(), error);
			if (error)
			{
				return false;
			}

			const auto temporary = path.wstring() + L".tmp";
			std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);

			for (const auto& address : updated)
			{
				stream << address << "\r\n";
			}

			stream.close();

			return !stream.fail() && MoveFileExW(temporary.data(), path.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
		}

		std::string get_map_display_name(const std::string& mapname)
		{
			const auto* display_name = game::UI_GetMapDisplayName(mapname.data());
			return display_name && *display_name ? display_name : mapname;
		}

		std::string get_gametype_display_name(const std::string& gametype)
		{
			const auto* display_name = game::UI_GetCustomizedGameTypeDefaultDisplayName(gametype.data());
			return display_name && *display_name ? display_name : gametype;
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

		void drop_server(const game::netadr_s& address, const std::string& challenge)
		{
			std::lock_guard<std::mutex> _{mutex};

			const auto queued = master_state.queued_servers.find(address);
			if (queued == master_state.queued_servers.end() || !queued->second.queried
				|| queued->second.challenge != challenge)
			{
				return;
			}

			master_state.queued_servers.erase(queued);
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
						// A lost follow-up probe must not discard an already discovered server.
						if (!queued.samples)
						{
							remove_server_locked(entry->first);
						}
						entry = master_state.queued_servers.erase(entry);
						continue;
					}

					if (!queued.queried && queued.challenge.empty() && now >= queued.next_query
						&& queries.size() < query_limit)
					{
						queued.challenge = utils::cryptography::random::get_challenge();

						queries.emplace_back(entry->first, queued.challenge);
					}

					++entry;
				}
			}

			for (const auto& [address, challenge] : queries)
			{
				{
					std::lock_guard<std::mutex> _{mutex};
					const auto queued = master_state.queued_servers.find(address);
					if (queued == master_state.queued_servers.end() || queued->second.queried
						|| queued->second.challenge != challenge)
					{
						continue;
					}

					// Time each send separately, excluding preparation and earlier sends in the batch.
					queued->second.query_start = std::chrono::steady_clock::now();
					queued->second.queried = true;
				}

				network::send(address, "s2x_getInfo", challenge);
			}
		}

		void refresh_server_list()
		{
			game::netadr_s master{};
			const auto has_master = master_server::get_address(master);

			{
				std::lock_guard<std::mutex> _{mutex};
				servers.clear();
				master_state = {};
				master_state.address = master;
				master_state.requesting = has_master;
				master_state.request_start = std::chrono::steady_clock::now();
				load_favorites_locked();

				for (const auto& favorite : favorites)
				{
					game::netadr_s address{};
					if (parse_favorite_address(favorite, address))
					{
						queue_server_locked(address);
					}
				}
			}

			if (!has_master)
			{
				return;
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

		ui_scripting::table get_server_snapshot()
		{
			// One snapshot keeps all row fields and actions tied to the same endpoint
			// even when new replies reorder the discovery list between UI polls.
			std::lock_guard<std::mutex> _{mutex};

			ui_scripting::table result{};
			ui_scripting::table entries{};
			int index = 1;

			const auto add_entry = [&](const server_info& server, const bool favorite)
			{
				ui_scripting::table entry{};
				entry["address"] = server.address_string;
				entry["hostname"] = server.hostname;
				entry["map"] = server.mapname;
				entry["mapReference"] = server.map_reference;
				entry["gametype"] = server.gametype;
				entry["clients"] = server.clients;
				entry["bots"] = server.bots;
				entry["maxClients"] = server.max_clients;
				entry["ping"] = server.ping;
				entry["status"] = server.status;
				entry["online"] = server.valid;
				entry["favorite"] = favorite;
				entries[index++] = entry;
			};

			for (const auto& server : servers)
			{
				const auto favorite = std::ranges::find(favorites, server.address_string) != favorites.end();
				if (server.valid || favorite)
				{
					add_entry(server, favorite);
				}
			}

			for (const auto& favorite : favorites)
			{
				if (std::ranges::none_of(servers, [&](const server_info& server)
				{
					return server.address_string == favorite;
				}))
				{
					server_info offline{};
					offline.address_string = favorite;
					offline.hostname = favorite;
					offline.status = "Offline";
					add_entry(offline, true);
				}
			}

			result["servers"] = entries;
			result["refreshing"] = master_state.requesting || !master_state.queued_servers.empty();

			return result;
		}

		bool toggle_favorite(const std::string& address)
		{
			std::lock_guard<std::mutex> _{mutex};

			auto updated = favorites;
			const auto existing = std::ranges::find(updated, address);
			if (existing != updated.end())
			{
				updated.erase(existing);
			}
			else
			{
				if (updated.size() >= server_limit || std::ranges::none_of(servers, [&](const server_info& server)
				{
					return server.valid && server.address_string == address;
				}))
				{
					return false;
				}

				updated.emplace_back(address);
			}

			if (!save_favorites_locked(updated))
			{
				console::warn("[server_list] could not save favorites\n");
				return false;
			}

			favorites = std::move(updated);

			return true;
		}

		bool join_server_address(const std::string& address)
		{
			{
				std::lock_guard<std::mutex> _{mutex};
				if (std::ranges::none_of(servers, [&](const server_info& server)
				{
					return server.valid && server.address_string == address;
				}))
				{
					return false;
				}
			}

			party::queue_connect(address);

			return true;
		}

		void handle_info_response(const game::netadr_s& from, const std::string_view& data)
		{
			// Parsing and display-name lookups are local work, not part of the round trip.
			const auto response_time = std::chrono::steady_clock::now();
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
				if (response_time < query_start || response_time - query_start > server_timeout)
				{
					return;
				}
			}

			if (info.get("gamename") != master_server::game_name || info.get("s2x") != "1")
			{
				drop_server(address, challenge);
				return;
			}

			const auto& mode = game::environment::get_online_mode_info();
			if (info.get("mode") != std::string{mode.token})
			{
				drop_server(address, challenge);
				return;
			}

			int protocol{};
			if (!parse_info_int(info.get("protocol"), 0, std::numeric_limits<int>::max(), protocol)
				|| protocol != PROTOCOL)
			{
				drop_server(address, challenge);
				return;
			}

			int server_running{};
			if (!parse_info_int(info.get("sv_running"), 0, 1, server_running))
			{
				drop_server(address, challenge);
				return;
			}

			const auto session_kind = party::session::classify(info.get("party_session"));
			if (session_kind == party::session::kind::invalid
				|| (!server_running && session_kind == party::session::kind::none))
			{
				drop_server(address, challenge);
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
				drop_server(address, challenge);
				return;
			}

			const auto map_display_name = get_map_display_name(mapname);
			const auto gametype_display_name = get_gametype_display_name(gametype);
			const auto ping = static_cast<int>(std::min(
				std::chrono::duration_cast<std::chrono::milliseconds>(response_time - query_start),
				999ms).count());

			{
				std::lock_guard<std::mutex> _{mutex};

				const auto queued = master_state.queued_servers.find(address);
				if (queued == master_state.queued_servers.end() || !queued->second.queried
					|| queued->second.challenge != challenge)
				{
					return;
				}

				auto* server = get_server_by_address(address);
				if (!server)
				{
					return;
				}

				if (++queued->second.samples >= ping_sample_count)
				{
					master_state.queued_servers.erase(queued);
				}
				else
				{
					queued->second.queried = false;
					queued->second.challenge.clear();
					queued->second.next_query = response_time + ping_sample_interval;
				}

				server->hostname = hostname.empty() ? server->address_string : hostname;
				server->mapname = map_display_name;
				server->map_reference = mapname;
				server->gametype = gametype_display_name;
				server->clients = clients;
				server->bots = bots;
				server->max_clients = max_clients;
				// The least delayed sample reduces frame/processing jitter in the browser estimate.
				server->ping = server->valid ? std::min(server->ping, ping) : ping;
				// Bots yield their slots to joining players.
				server->status = clients - bots >= max_clients ? "Full" : "Joinable";
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

			lobby["S2xBuildServerList"] = [](int)
			{
				update_server_display_list();
			};

			lobby["S2xRefreshServerList"] = [](int, int)
			{
				refresh_server_list();
			};

			lobby["S2xUpdateServerDisplayList"] = [](int)
			{
				update_server_display_list();
			};

			lobby["S2xGetServerCount"] = [](int)
			{
				return get_server_count();
			};

			lobby["S2xGetServerData"] = [](int, int index, int column)
			{
				return get_server_data(index, column);
			};

			lobby["S2xJoinServer"] = [](int, int index)
			{
				join_server(index);
			};

			lobby["CancelS2xConnection"] = []
			{
				party::cancel_pending_connection();
			};

			lobby["GetServerSnapshot"] = []
			{
				return get_server_snapshot();
			};

			lobby["ToggleFavorite"] = [](const std::string& address)
			{
				return toggle_favorite(address);
			};

			lobby["JoinServerAddress"] = [](const std::string& address)
			{
				return join_server_address(address);
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
