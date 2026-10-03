#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console/console.hpp"
#include "network.hpp"
#include "party.hpp"
#include "scheduler.hpp"
#include "server_commands.hpp"

#include "game/game.hpp"

#include <charconv>

namespace server_commands
{
	namespace
	{
		constexpr auto rcon_timeout = 1s;

		game::dvar_t* rcon_password{};

		std::recursive_mutex redirect_mutex;
		bool redirecting{};
		game::netadr_s redirect_target{};
		std::string redirect_buffer{};

		constexpr auto client_zombie = 1;
		constexpr auto client_connected = 3;
		constexpr auto client_active = 5;

		template <size_t Size>
		std::string status_string(const char (&value)[Size])
		{
			std::string result{};

			for (size_t i = 0; i < Size && value[i]; ++i)
			{
				// Strip S2 color codes and control characters to keep one row per client
				// in the terminal, graphical console, and log file.
				if (value[i] == '^' && i + 1 < Size && value[i + 1] >= '0' && value[i + 1] <= ';')
				{
					++i;
					continue;
				}

				const auto character = static_cast<unsigned char>(value[i]);
				result.push_back(character < ' ' || character == 0x7F ? ' ' : value[i]);
			}

			return result;
		}

		game::mp::client_t* get_kick_client(const unsigned int slot)
		{
			if (!game::is_server_running())
			{
				console::info("Server is not running.\n");
				return nullptr;
			}

			const auto max_clients = *game::sv_maxclients;
			if (max_clients <= 0 || slot >= static_cast<unsigned int>(max_clients))
			{
				console::info("Invalid client slot %u. Use status to find a client slot.\n", slot);
				return nullptr;
			}

			auto* clients = *game::mp::svs_clients;
			if (!clients || clients[slot].state <= client_zombie)
			{
				console::info("Client slot %u is empty or disconnecting.\n", slot);
				return nullptr;
			}

			auto& client = clients[slot];
			if (client.remoteAddress.type == game::NA_LOOPBACK)
			{
				console::info("Cannot kick the local/host client in slot %u.\n", slot);
				return nullptr;
			}

			if (client.testClient || client.remoteAddress.type == game::NA_BOT)
			{
				console::info("Cannot kick a bot in slot %u.\n", slot);
				return nullptr;
			}

			if (!client.guid[0])
			{
				console::info("Client slot %u has no identity yet. Try again after it connects.\n", slot);
				return nullptr;
			}

			return &client;
		}

		void queue_kick(const unsigned int slot)
		{
			const auto* client = get_kick_client(slot);
			if (!client)
			{
				return;
			}

			// Keep the connection identity, not a client pointer, across the thread handoff.
			scheduler::once([slot, guid = std::to_array(client->guid), address = client->remoteAddress,
				qport = client->qport, connect_time = client->lastConnectTime]
			{
				auto* target = get_kick_client(slot);
				if (!target)
				{
					return;
				}

				if (std::to_array(target->guid) != guid || target->remoteAddress != address ||
					target->qport != qport || target->lastConnectTime != connect_time)
				{
					console::info("Client slot %u changed before the kick could run. Use status and try again.\n", slot);
					return;
				}

				const auto name = status_string(target->name);
				// SV_KickClient can blacklist GUIDs via sv_blacklistReasons. Only disconnect here.
				game::mp::SV_DropClient(target, "EXE_PLAYERKICKED", 1);
				target->lastPacketTime = *game::mp::svs_time;
				console::info("Kicked client %u (%s).\n", slot, name.c_str());
			}, scheduler::server);
		}

		void clientkick(const command::params& params)
		{
			if (params.size() != 2)
			{
				console::info("Usage: clientkick <slot> (use status to find the slot).\n");
				return;
			}

			const std::string_view text = params[1];
			unsigned int slot{};
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), slot);
			if (error != std::errc{} || end != text.data() + text.size())
			{
				console::info("Invalid slot: enter a non-negative decimal slot number from status.\n");
				return;
			}

			queue_kick(slot);
		}

		void kick(const command::params& params)
		{
			if (params.size() != 2)
			{
				console::info("Usage: kick <name> (exact name from status; quote names containing spaces).\n");
				return;
			}

			const std::string_view name = params[1];
			if (name.empty() || name.size() >= sizeof(game::mp::client_t::name) ||
				std::any_of(name.begin(), name.end(), [](const unsigned char c) { return c < ' ' || c == 0x7F; }))
			{
				console::info("Invalid player name. Enter the exact name shown by status.\n");
				return;
			}

			if (!game::is_server_running())
			{
				console::info("Server is not running.\n");
				return;
			}

			const auto* clients = *game::mp::svs_clients;
			const auto max_clients = *game::sv_maxclients;
			auto slot = -1;

			for (auto i = 0; clients && i < max_clients; ++i)
			{
				if (clients[i].state <= client_zombie || status_string(clients[i].name) != name)
				{
					continue;
				}
				if (slot != -1)
				{
					console::info("Multiple clients have that name. Use status and clientkick <slot>.\n");
					return;
				}
				slot = i;
			}
			
			if (slot == -1)
			{
				console::info("No client has that exact name (case-sensitive). Use status to list clients.\n");
				return;
			}

			queue_kick(static_cast<unsigned int>(slot));
		}

		void status()
		{
			if (!game::is_server_running())
			{
				console::info("Server is not running.\n");
				return;
			}

			constexpr auto row_format = "{:>3} {:>5} {:>4} {:16} {:36} {:>7} {:21} {:>6} {:>5}\n";
			auto output = std::format("map: {}\n", party::loaded_map_name());
			output += std::format(row_format, "num", "score", "ping", "guid", "name", "lastmsg", "address", "qport", "rate");
			output += std::format(row_format, "---", "-----", "----", "----------------", "------------------------------------",
				"-------", "---------------------", "------", "-----");

			const auto* clients = *game::mp::svs_clients;
			const auto max_clients = *game::sv_maxclients;
			const auto server_time = static_cast<std::int64_t>(*game::mp::svs_time);
			
			for (auto i = 0; clients && i < max_clients; ++i)
			{
				const auto& client = clients[i];
				if (!client.state)
				{
					continue;
				}

				// S2 adds a reconnecting state (2) and finishes loading in state 5.
				const auto ping = client.state == client_zombie ? "ZMBI"
					: client.state < client_active ? "CNCT"
					: std::format("{:4}", std::clamp(client.ping, 0, 9999));
				const auto score = client.state >= client_connected && client.gentity && client.gentity->client
					? client.gentity->client->score : 0;
				const auto last_message = std::max<std::int64_t>(0, server_time - client.lastPacketTime);

				output += std::format(row_format,
					i, score, ping, status_string(client.guid), status_string(client.name), last_message,
					network::net_adr_to_string(client.remoteAddress), client.qport, client.rate);
			}

			output += '\n';
			console::dispatch_message(console::print_type_info, output);
		}

		void send_print(const game::netadr_s& target, const std::string& text)
		{
			constexpr std::size_t max_packet_size = 0x4EE;
			constexpr std::size_t max_chunk_size = max_packet_size - (sizeof("\xFF\xFF\xFF\xFF" "print\n") - 1);

			if (text.size() <= max_chunk_size)
			{
				network::send(target, "print", text, '\n');
				return;
			}

			std::size_t offset = 0;
			while (offset < text.size())
			{
				auto length = std::min(max_chunk_size, text.size() - offset);
				if (offset + length < text.size())
				{
					const auto newline = text.rfind('\n', offset + length - 1);
					if (newline != std::string::npos && newline >= offset)
					{
						length = newline - offset + 1;
					}
				}

				network::send(target, "print", text.substr(offset, length), '\n');
				offset += length;
			}
		}

		std::string join_args(const command::params& params, const int index)
		{
			std::string result{};

			for (auto i = index; i < params.size(); ++i)
			{
				const std::string_view arg = params[i];
				if (i > index)
				{
					result.push_back(' ');
				}

				if (arg.empty() || arg.find_first_of(" \t;") != std::string_view::npos)
				{
					result.push_back('"');
					result.append(arg);
					result.push_back('"');
				}
				else
				{
					result.append(arg);
				}
			}

			return result;
		}

		bool is_redirecting()
		{
			std::lock_guard _(redirect_mutex);
			return redirecting;
		}

		bool setup_redirect(const game::netadr_s& target)
		{
			std::lock_guard _(redirect_mutex);
			if (redirecting)
			{
				return false;
			}

			redirecting = true;
			redirect_target = target;
			redirect_buffer.clear();
			return true;
		}

		void clear_redirect()
		{
			game::netadr_s target{};
			std::string buffer{};

			{
				std::lock_guard _(redirect_mutex);
				target = redirect_target;
				buffer = std::move(redirect_buffer);
				redirecting = false;
				redirect_target = {};
				redirect_buffer.clear();
			}

			send_print(target, buffer);
		}

		void finish_redirect()
		{
			const auto deadline = std::chrono::steady_clock::now() + rcon_timeout;
			auto first_frame = true;

			scheduler::schedule([deadline, first_frame]() mutable
			{
				if (first_frame)
				{
					first_frame = false;
					return scheduler::cond_continue;
				}

				if (game::cmd_textArray[0].cmdsize > 0 && std::chrono::steady_clock::now() < deadline)
				{
					return scheduler::cond_continue;
				}

				clear_redirect();
				return scheduler::cond_end;
			}, scheduler::main);
		}

		void handle_rcon(const game::netadr_s& address, const std::string_view& data)
		{
			const auto separator = data.find(' ');
			if (separator == std::string_view::npos)
			{
				network::send(address, "print", "Invalid RCon request", '\n');
				return;
			}

			const auto password = data.substr(0, separator);
			auto rcon_command = std::string{data.substr(separator + 1)};
			while (!rcon_command.empty() && (rcon_command.back() == '\0' || rcon_command.back() == '\n'
				|| rcon_command.back() == '\r'))
			{
				rcon_command.pop_back();
			}

			if (rcon_command.empty() || !rcon_password || !rcon_password->current.string
				|| !*rcon_password->current.string)
			{
				return;
			}

			if (password != rcon_password->current.string)
			{
				network::send(address, "print", "Invalid rcon password", '\n');
				console::error("Invalid rcon password from %s\n", network::net_adr_to_string(address));
				return;
			}

			if (is_redirecting())
			{
				network::send(address, "print", "RCon is busy, try again", '\n');
				return;
			}

			console::info("RCon from %s: %s\n", network::net_adr_to_string(address), rcon_command.data());

			if (!setup_redirect(address))
			{
				network::send(address, "print", "RCon is busy, try again", '\n');
				return;
			}

			rcon_command.push_back('\n');
			game::Cbuf_AddText(0, rcon_command.data());
			finish_redirect();
		}

		void rcon(const command::params& params)
		{
			static std::string password{};

			if (params.size() < 2)
			{
				console::info("Usage: rcon login <password> | rcon logout | rcon <command>\n");
				return;
			}

			const std::string_view operation = params[1];
			if (operation == "login")
			{
				if (params.size() < 3)
				{
					return;
				}

				password = params.join(2);
				return;
			}

			if (operation == "logout")
			{
				password.clear();
				return;
			}

			const auto data = join_args(params, 1);
			if (game::is_server_running())
			{
				game::Cbuf_AddText(0, (data + "\n").data());
				return;
			}

			if (password.empty())
			{
				console::info("You must login first to use RCon\n");
				return;
			}

			const auto& target = party::get_target();
			if (target.type <= game::NA_BAD || !game::CL_IsLocalClientInGame(0))
			{
				console::warn("You need to be connected to a server!\n");
				return;
			}

			network::send(target, "rcon", password + " " + data);
		}
	}

	bool message_redirect(const std::string& message)
	{
		std::lock_guard _(redirect_mutex);
		if (!redirecting)
		{
			return false;
		}

		redirect_buffer.append(message);
		return true;
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			command::add("status", status);
			command::add("clientkick", clientkick);
			command::add("kick", kick);

			if (game::environment::is_dedicated())
			{
				scheduler::once([]
				{
					rcon_password = game::Dvar_RegisterString("rcon_password", "", game::DVAR_FLAG_NONE);
				}, scheduler::pipeline::main);

				network::on("rcon", handle_rcon);
			}
			else
			{
				command::add("rcon", rcon);
			}
		}
	};
}

REGISTER_COMPONENT(server_commands::component)
