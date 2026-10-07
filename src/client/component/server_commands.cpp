#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console/console.hpp"
#include "dedicated_party.hpp"
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
		constexpr auto chat_command = 'T';
		constexpr std::size_t max_chat_length = 512;
		constexpr std::size_t max_kick_reason_length = 768;

		game::dvar_t* rcon_password{};
		game::dvar_t* sv_say_name{};

		std::recursive_mutex redirect_mutex;
		bool redirecting{};
		game::netadr_s redirect_target{};
		std::string redirect_buffer{};

		constexpr auto client_zombie = 1;
		constexpr auto client_connected = 3;
		constexpr auto client_primed = 4;
		constexpr auto client_active = 5;

		std::string status_string(const std::string_view value)
		{
			std::string result{};

			for (size_t i = 0; i < value.size() && value[i]; ++i)
			{
				// Strip S2 color codes and control characters to keep one row per client
				// in the terminal, graphical console, and log file.
				if (value[i] == '^' && i + 1 < value.size() && value[i + 1] >= '0' && value[i + 1] <= ';')
				{
					++i;
					continue;
				}

				const auto character = static_cast<unsigned char>(value[i]);
				result.push_back(character < ' ' || character == 0x7F ? ' ' : value[i]);
			}

			return result;
		}

		template <size_t Size>
		std::string status_string(const char (&value)[Size])
		{
			return status_string(std::string_view{value, strnlen(value, Size)});
		}

		std::string sanitize_text(std::string text, const std::size_t max_length, const bool keep_quotes)
		{
			for (auto& character : text)
			{
				const auto value = static_cast<unsigned char>(character);
				if (value < ' ' || value == 0x7F)
				{
					character = ' ';
				}
				else if (character == '"' && !keep_quotes)
				{
					character = '\'';
				}
			}

			const auto first = text.find_first_not_of(' ');
			if (first == std::string::npos)
			{
				return {};
			}

			text = text.substr(first, text.find_last_not_of(' ') - first + 1);
			if (text.size() > max_length)
			{
				text.resize(max_length);
			}

			return text;
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

		void drop_client(game::mp::client_t& client, const std::string& reason)
		{
			// SV_KickClient can blacklist GUIDs via sv_blacklistReasons. Only disconnect here.
			if (reason.empty())
			{
				game::mp::SV_DropClient(&client, "EXE_PLAYERKICKED", 1);
				return;
			}

			// Lowercase passes the client's kick check but skips its "EXE_<key>:<code>" parse; 0x1F starts literal text.
			std::string message = "exe_playerkicked";
			message.push_back('\x1F');
			message.append(" - ");
			message.append(reason);

			if (client.state == client_active)
			{
				game::SV_SendServerCommand(&client, game::SV_CMD_RELIABLE, "%c \"%s\"", 'r', message.data());
			}
			else
			{
				network::send(client.remoteAddress, "disconnect", "\"" + message + "\"");
			}

			// Others still see the stock kick line rather than the custom reason.
			game::mp::SV_DropClient(&client, "EXE_PLAYERKICKED", 0);
		}

		bool kick_from_lobby(const unsigned int slot, const std::string& reason)
		{
			const auto lobby = dedicated_party::get_lobby_status();
			if (!lobby)
			{
				return false;
			}

			const auto member = std::ranges::find(lobby->members, static_cast<int>(slot), &dedicated_party::lobby_member::slot);
			if (member == lobby->members.end() || !dedicated_party::kick_lobby_member(member->index, reason))
			{
				console::info("Lobby slot %u is empty. Use status to find a lobby member.\n", slot);
				return true;
			}

			console::info("Kicked lobby member %u (%s).\n", slot, status_string(member->name).data());
			return true;
		}

		void queue_kick(const unsigned int slot, std::string reason)
		{
			if (kick_from_lobby(slot, reason))
			{
				return;
			}

			const auto* client = get_kick_client(slot);
			if (!client)
			{
				return;
			}

			// Keep the connection identity, not a client pointer, across the thread handoff.
			scheduler::once([slot, guid = std::to_array(client->guid), address = client->remoteAddress,
				qport = client->qport, connect_time = client->lastConnectTime, reason = std::move(reason)]
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
				drop_client(*target, reason);
				target->lastPacketTime = *game::mp::svs_time;
				console::info("Kicked client %u (%s).\n", slot, name.c_str());
			}, scheduler::server);
		}

		std::string get_kick_reason(const command::params& params, const int index)
		{
			return sanitize_text(params.join(index), max_kick_reason_length, false);
		}

		std::optional<unsigned int> parse_slot(const std::string_view text)
		{
			unsigned int slot{};
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), slot);
			if (error != std::errc{} || end != text.data() + text.size())
			{
				console::info("Invalid slot: enter a non-negative decimal slot number from status.\n");
				return {};
			}

			return slot;
		}

		void clientkick(const command::params& params)
		{
			if (params.size() < 2)
			{
				console::info("Usage: clientkick <slot> [reason] (use status to find the slot).\n");
				return;
			}

			if (const auto slot = parse_slot(params[1]))
			{
				queue_kick(*slot, get_kick_reason(params, 2));
			}
		}

		void kick(const command::params& params)
		{
			if (params.size() < 2)
			{
				console::info("Usage: kick <name> [reason] (exact name from status; quote names containing spaces).\n");
				return;
			}

			const std::string_view name = params[1];
			if (name.empty() || name.size() >= sizeof(game::mp::client_t::name) ||
				std::any_of(name.begin(), name.end(), [](const unsigned char c) { return c < ' ' || c == 0x7F; }))
			{
				console::info("Invalid player name. Enter the exact name shown by status.\n");
				return;
			}

			std::vector<int> matches{};
			if (game::is_server_running())
			{
				const auto* clients = *game::mp::svs_clients;
				const auto max_clients = *game::sv_maxclients;

				for (auto i = 0; clients && i < max_clients; ++i)
				{
					if (clients[i].state > client_zombie && status_string(clients[i].name) == name)
					{
						matches.push_back(i);
					}
				}
			}
			else if (const auto lobby = dedicated_party::get_lobby_status())
			{
				for (const auto& member : lobby->members)
				{
					if (status_string(member.name) == name)
					{
						matches.push_back(member.slot);
					}
				}
			}
			else
			{
				console::info("Server is not running.\n");
				return;
			}

			if (matches.size() > 1)
			{
				console::info("Multiple clients have that name. Use status and clientkick <slot>.\n");
				return;
			}

			if (matches.empty())
			{
				console::info("No client has that exact name (case-sensitive). Use status to list clients.\n");
				return;
			}

			queue_kick(static_cast<unsigned int>(matches.front()), get_kick_reason(params, 2));
		}

		void send_chat(const std::optional<unsigned int> slot, const std::string& message)
		{
			if (!game::is_server_running())
			{
				console::info(dedicated_party::get_lobby_status()
					? "No match is running; lobby members cannot receive chat.\n"
					: "Server is not running.\n");
				return;
			}

			if (message.empty())
			{
				return;
			}

			scheduler::once([slot, message]
			{
				if (!game::is_server_running())
				{
					return;
				}

				if (!slot)
				{
					game::SV_SendServerCommand(nullptr, game::SV_CMD_CAN_IGNORE, "%c \"%s\"", chat_command, message.data());
					return;
				}

				auto* clients = *game::mp::svs_clients;
				if (!clients || *slot >= static_cast<unsigned int>(*game::sv_maxclients) || clients[*slot].state < client_primed)
				{
					console::info("Client slot %u is not in the game.\n", *slot);
					return;
				}

				game::SV_SendServerCommand(&clients[*slot], game::SV_CMD_CAN_IGNORE, "%c \"%s\"", chat_command, message.data());
			}, scheduler::server);
		}

		std::string get_say_prefix()
		{
			const auto* name = sv_say_name && sv_say_name->current.string ? sv_say_name->current.string : "";
			return std::string{name} + "^7: ";
		}

		void say(const command::params& params, const bool raw)
		{
			if (params.size() < 2)
			{
				console::info("Usage: %s <message>\n", params[0]);
				return;
			}

			const auto message = sanitize_text(params.join(1), max_chat_length, true);
			const auto text = raw ? message : get_say_prefix() + message;
			console::info("%s\n", text.data());
			send_chat({}, text);
		}

		void tell(const command::params& params, const bool raw)
		{
			if (params.size() < 3)
			{
				console::info("Usage: %s <slot> <message> (use status to find the slot).\n", params[0]);
				return;
			}

			const auto slot = parse_slot(params[1]);
			if (!slot)
			{
				return;
			}

			const auto message = sanitize_text(params.join(2), max_chat_length, true);
			const auto text = raw ? message : get_say_prefix() + message;
			console::info("%u: %s\n", *slot, text.data());
			send_chat(slot, text);
		}

		constexpr auto status_row_format = "{:>3} {:>5} {:>4} {:16} {:36} {:>7} {:21} {:>6} {:>5}\n";

		std::string get_status_header(const std::string& map_name, const std::string& gametype)
		{
			auto output = std::format("map: {}\ngametype: {}\n", map_name, gametype);
			output += std::format(status_row_format, "num", "score", "ping", "guid", "name", "lastmsg", "address", "qport", "rate");
			output += std::format(status_row_format, "---", "-----", "----", "----------------", "------------------------------------",
				"-------", "---------------------", "------", "-----");
			return output;
		}

		void print_lobby_status(const dedicated_party::lobby_status& lobby)
		{
			// Lobby members are party members, not gameplay clients; the slot is the one they play in.
			auto output = get_status_header(lobby.map_name, lobby.gametype);
			for (const auto& member : lobby.members)
			{
				output += std::format(status_row_format,
					member.slot, 0, member.joining ? "CNCT" : "0", std::format("{:016x}", member.xuid),
					status_string(member.name), 0, member.address, 0, 0);
			}

			output += '\n';
			console::dispatch_message(console::print_type_info, output);
		}

		void status()
		{
			if (!game::is_server_running())
			{
				if (const auto lobby = dedicated_party::get_lobby_status())
				{
					print_lobby_status(*lobby);
					return;
				}

				console::info("Server is not running.\n");
				return;
			}

			auto output = get_status_header(party::loaded_map_name(), party::loaded_gametype());

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

				output += std::format(status_row_format,
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

		std::string get_command_args_text(const command::params& params)
		{
			const auto nesting = game::cmd_args->nesting;
			const auto* text = nesting >= 0 && nesting < game::CMD_MAX_NESTING ? game::cmd_args->text[nesting] : nullptr;
			if (!text)
			{
				return params.join(1);
			}

			std::string_view view = text;
			const auto is_space = [](const char c)
			{
				return c == ' ' || c == '\t';
			};

			while (!view.empty() && is_space(view.front()))
			{
				view.remove_prefix(1);
			}

			while (!view.empty() && !is_space(view.front()))
			{
				view.remove_prefix(1);
			}

			while (!view.empty() && is_space(view.front()))
			{
				view.remove_prefix(1);
			}

			while (!view.empty() && (view.back() == '\n' || view.back() == '\r'))
			{
				view.remove_suffix(1);
			}

			return std::string{view};
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

		void join_quoted_lines(std::string& text)
		{
			// Cbuf splits on every newline, so a multi-line quoted argument would run its tail as commands.
			auto quoted = false;
			for (auto& character : text)
			{
				if (character == '"')
				{
					quoted = !quoted;
				}
				else if (quoted && (character == '\n' || character == '\r'))
				{
					character = ' ';
				}
			}
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

			if (!setup_redirect(address))
			{
				network::send(address, "print", "RCon is busy, try again", '\n');
				return;
			}

			join_quoted_lines(rcon_command);
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

			const auto data = get_command_args_text(params);
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
					sv_say_name = game::Dvar_RegisterString("sv_sayName", "Console", game::DVAR_FLAG_NONE);
				}, scheduler::pipeline::main);

				// Dedicated only: on clients these would shadow the player's own console say.
				command::add("say", [](const command::params& params) { say(params, false); });
				command::add("sayraw", [](const command::params& params) { say(params, true); });
				command::add("tell", [](const command::params& params) { tell(params, false); });
				command::add("tellraw", [](const command::params& params) { tell(params, true); });

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
