#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console/console.hpp"
#include "network.hpp"
#include "party.hpp"

#include "game/game.hpp"

namespace server_commands
{
	namespace
	{
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

		void status()
		{
			if (!game::is_server_running())
			{
				console::info("Server is not running.\n");
				return;
			}

			auto output = std::format("map: {}\n", party::loaded_map_name());
			output += "num score ping guid                             name                                 lastmsg address               qport rate\n";
			output += "--- ----- ---- -------------------------------- ------------------------------------ ------- --------------------- ----- -----\n";

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

				output += std::format("{:3} {:5} {} {:>32} {:36} {:7} {:21} {:5} {:5}\n",
					i, score, ping, status_string(client.guid), status_string(client.name), last_message,
					network::net_adr_to_string(client.remoteAddress), client.qport, client.rate);
			}

			output += '\n';
			console::dispatch_message(console::print_type_info, output);
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			command::add("status", status);
		}
	};
}

REGISTER_COMPONENT(server_commands::component)
