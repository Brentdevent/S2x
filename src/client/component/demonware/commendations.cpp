#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "commendations.hpp"
#include "component/network.hpp"
#include "component/party.hpp"
#include "component/scheduler.hpp"
#include "game/demonware/economy_tools.hpp"
#include "game/demonware/player_vote.hpp"
#include "game/demonware/runtime_context.hpp"
#include "game/game.hpp"

#include <utils/cryptography.hpp>
#include <utils/hook.hpp>
#include <utils/string.hpp>

#include <charconv>
#include <condition_variable>

namespace commendations
{
	namespace
	{
		namespace vote = demonware::player_vote;
		constexpr auto command_name = "$s2x_commend";
		constexpr auto timeout = 5s;

		struct request
		{
			std::uint64_t giver, recipient, nonce;
			std::uint32_t day;
			bool done{}, accepted{};
		};

		struct peer
		{
			int slot;
			std::uint64_t user;
			game::netadr_s address;
			int qport, connected;
		};

		struct route
		{
			peer giver, recipient;
			std::uint64_t nonce;
			std::uint32_t day;
			std::chrono::steady_clock::time_point expires;
		};

		std::mutex request_mutex;
		std::condition_variable response_ready;
		std::shared_ptr<request> pending;
		std::atomic_uint64_t connection_generation{};
		std::atomic_bool accepting{};
		// Server-thread only; at most one pending delivery per connected giver.
		std::unordered_map<int, route> routes;

		bool number(const std::string_view text, std::uint64_t& value)
		{
			const auto end = text.data() + text.size();
			const auto parsed = std::from_chars(text.data(), end, value, 16);
			return !text.empty() && text.size() <= 16 && parsed.ec == std::errc{} && parsed.ptr == end;
		}

		std::optional<peer> player(const int slot)
		{
			if (!game::SV_Loaded() || slot < 0 || slot >= *game::sv_maxclients)
			{
				return {};
			}

			auto* party = game::Live_GetGameParty();
			auto* clients = *game::mp::svs_clients;
			if (!party || !clients)
			{
				return {};
			}

			const auto& client = clients[slot];
			std::uint64_t user{};
			if (client.state != 5 || client.testClient || client.remoteAddress.type == game::NA_BOT ||
				!number({client.guid, strnlen(client.guid, sizeof(client.guid))}, user) || !user ||
				game::Party_FindMemberByXUID(party, user) != slot)
			{
				return {};
			}

			return peer{slot, user, client.remoteAddress, client.qport, client.lastConnectTime};
		}

		bool connected(const peer& expected)
		{
			const auto current = player(expected.slot);
			return current && current->user == expected.user && current->address == expected.address &&
				current->qport == expected.qport && current->connected == expected.connected;
		}

		void forward(const std::string& text)
		{
			const command::params tokens{text};
			game::CL_ForwardCommandToServer(0, text.c_str());
		}

		void server_command(const int slot, const command::params_sv& args)
		{
			if (!accepting || game::environment::is_zombies() || party::loaded_gametype() != "hub" ||
				(args.size() != 5 && args.size() != 6))
			{
				return;
			}

			const auto sender = player(slot);
			std::uint64_t target{}, nonce{}, day{}, accepted{};
			if (!sender || !number(args[2], target) || !target || target == sender->user ||
				!number(args[3], nonce) || !nonce || !number(args[4], day) || day != vote::period())
			{
				return;
			}

			const auto now = std::chrono::steady_clock::now();
			std::erase_if(routes, [now](const auto& entry)
			{
				return entry.second.expires <= now || !connected(entry.second.giver) || !connected(entry.second.recipient);
			});

			const auto target_slot = game::Party_FindMemberByXUID(game::Live_GetGameParty(), target);
			const auto recipient = player(target_slot);
			if (!recipient || recipient->user != target)
			{
				return;
			}

			if (std::string_view{args[1]} == "request" && args.size() == 5)
			{
				if (routes.contains(slot))
				{
					return;
				}

				routes.emplace(slot, route{*sender, *recipient, nonce, static_cast<std::uint32_t>(day), now + timeout});
				game::SV_SendServerCommand(&(*game::mp::svs_clients)[recipient->slot], game::SV_CMD_RELIABLE,
					"%s grant %llx %llx %llx %llx", command_name, sender->user, target, nonce, day);
			}
			else if (std::string_view{args[1]} == "ack" && args.size() == 6 && number(args[5], accepted) && accepted <= 1)
			{
				const auto found = routes.find(recipient->slot);
				if (found == routes.end() || found->second.recipient.user != sender->user ||
					found->second.nonce != nonce || found->second.day != day)
				{
					return;
				}

				game::SV_SendServerCommand(&(*game::mp::svs_clients)[recipient->slot], game::SV_CMD_RELIABLE,
					"%s result %llx %llx %llx %llx", command_name, sender->user, nonce, day, accepted);
				routes.erase(found);
			}
		}

		void receive_grant(const std::uint64_t giver, const std::uint64_t recipient,
			const std::uint64_t nonce, const std::uint32_t day)
		{
			const auto identity = demonware::runtime_context::get_snapshot();
			if (!identity || identity->user_id != recipient || !game::CL_IsLocalClientInGame(0) ||
				game::environment::is_zombies() || day != vote::period())
			{
				return;
			}

			const auto result = vote::receive(giver, recipient, day);
			const auto succeeded = demonware::economy_tools::succeeded(result.status);
			if (succeeded)
			{
				const auto state = demonware::marketplace_store::get_snapshot();
				for (const auto& currency : state.currencies)
				{
					if (currency.currency_id == vote::social_currency)
					{
						// The stock conversion/reward path sets an absolute balance and
						// emits inventory/BalanceUpdated, including Social Rank threshold events.
						utils::hook::invoke<void>(0x27D510_g, 0, static_cast<int>(vote::social_currency), static_cast<int>(currency.value));
						break;
					}
				}
			}

			forward(utils::string::va("%s ack %llx %llx %x %x", command_name, giver, nonce, day, succeeded ? 1 : 0));
		}
	}

	bool give(const std::uint64_t giver, const std::uint64_t recipient, const std::uint32_t day)
	{
		if (!giver || !recipient || giver == recipient || !accepting)
		{
			return false;
		}

		std::unique_lock lock{request_mutex};
		if (pending)
		{
			return false;
		}

		std::uint64_t nonce{};
		utils::cryptography::random::get_data(&nonce, sizeof(nonce));
		const auto value = std::make_shared<request>(request{giver, recipient, nonce | 1, day});
		pending = value;
		scheduler::once([value]
		{
			std::lock_guard guard{request_mutex};
			if (pending != value || value->done || !accepting)
			{
				return;
			}

			if (!game::CL_IsLocalClientInGame(0) || game::environment::is_zombies() ||
				demonware::runtime_context::get_local_user_id() != value->giver)
			{
				value->done = true;
				response_ready.notify_all();
				return;
			}

			forward(utils::string::va("%s request %llx %llx %x", command_name,
				value->recipient, value->nonce, value->day));
		}, scheduler::pipeline::main);

		// Keep the reply on the DW service thread and its current socket. Never
		// acknowledge a vote before the recipient has persisted it. A lost reply
		// or failed giver save is safe to retry using the same pair/day receipt.
		response_ready.wait_for(lock, timeout, [&] { return value->done; });
		const auto accepted = value->accepted;
		pending.reset();
		return accepted;
	}

	bool receive(const unsigned local_client, const command::params& args)
	{
		if (!args.size() || std::string_view{args[0]} != command_name)
		{
			return false;
		}

		if (local_client || args.size() != 6 || !accepting)
		{
			return true;
		}

		std::array<std::uint64_t, 4> fields{};
		for (std::size_t i = 0; i < fields.size(); ++i)
		{
			if (!number(args[static_cast<int>(i) + 2], fields[i]))
			{
				return true;
			}
		}

		if (std::string_view{args[1]} == "grant" && fields[0] && fields[1] && fields[2] && fields[3] <= UINT32_MAX)
		{
			const auto generation = connection_generation.load();
			scheduler::once([fields, generation]
			{
				if (accepting && generation == connection_generation.load())
				{
					receive_grant(fields[0], fields[1], fields[2], static_cast<std::uint32_t>(fields[3]));
				}
			}, scheduler::pipeline::main);
		}
		else if (std::string_view{args[1]} == "result" && fields[3] <= 1)
		{
			std::lock_guard lock{request_mutex};
			if (pending && !pending->done && pending->recipient == fields[0] && pending->nonce == fields[1] && pending->day == fields[2])
			{
				pending->accepted = fields[3] != 0;
				pending->done = true;
				response_ready.notify_all();
			}
		}

		return true;
	}

	void disconnect()
	{
		++connection_generation;
		std::lock_guard lock{request_mutex};
		if (pending)
		{
			pending->done = true;
			response_ready.notify_all();
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			accepting = true;
			command::add_sv(command_name, server_command);
		}

		void pre_destroy() override
		{
			accepting = false;
			disconnect();
		}
	};
}

REGISTER_COMPONENT(commendations::component)
