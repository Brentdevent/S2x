#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "match_drops.hpp"
#include "economy.hpp"
#include "component/command.hpp"
#include "component/console/console.hpp"
#include "component/custom_match.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "game/demonware/economy_tools.hpp"
#include "game/demonware/loot_catalog.hpp"
#include "game/demonware/match_drop_reward.hpp"
#include "game/demonware/runtime_context.hpp"

#include <utils/cryptography.hpp>
#include <utils/hook.hpp>

#include <charconv>

namespace match_drops
{
	namespace
	{
		namespace reward = demonware::match_drop_reward;
		constexpr std::size_t pending_award_limit = 64;
		game::dvar_t* sv_match_supply_drops{};
		std::unordered_map<std::string, reward::award> pending;

		bool settle(const reward::award& award)
		{
			const auto identity = demonware::runtime_context::get_snapshot();
			if (!identity || !demonware::loot_catalog::get_snapshot())
			{
				return false;
			}
			if (identity->user_id != award.user)
			{
				return true;
			}

			const auto result = reward::grant(award);
			if (result.status == demonware::marketplace_store::transaction_status::save_failed)
			{
				return false;
			}
			if (demonware::economy_tools::succeeded(result.status))
			{
				economy::request_inventory_refresh();
				if (result.status == demonware::marketplace_store::transaction_status::committed)
				{
					console::info("Match reward: %u Rare Supply Drop(s).\n", award.quantity);
				}
			}
			else
			{
				console::error("Match reward rejected (%u); no inventory was changed.\n",
					static_cast<unsigned>(result.status));
			}
			return true;
		}

		void queue(const reward::award& award)
		{
			if (pending.contains(award.match))
			{
				return;
			}
			if (pending.size() >= pending_award_limit || settle(award))
			{
				return;
			}

			// A received award survives returning to the lobby. Only unsaved awards
			// need volatile retries; successful deliveries use the permanent receipt.
			pending.emplace(award.match, award);
			scheduler::schedule([match = award.match]
			{
				const auto found = pending.find(match);
				if (found == pending.end())
				{
					return true;
				}
				if (!settle(found->second))
				{
					return false;
				}
				pending.erase(found);
				return true;
			}, scheduler::pipeline::main, 1s);
		}

		void award_drops()
		{
			if (game::environment::is_zombies() || !game::SV_Loaded() ||
				!sv_match_supply_drops->current.integer || !custom_match::progression_enabled())
			{
				return;
			}
			const auto* gametype = game::Dvar_FindMalleableVar("g_gametype");
			if (!gametype || !gametype->current.string || std::string_view{gametype->current.string} == "hub")
			{
				return;
			}

			auto* clients = *game::mp::svs_clients;
			auto* party = game::Live_GetGameParty();
			if (!clients || !party)
			{
				return;
			}

			std::vector<std::uint64_t> users;
			for (int slot = 0; slot < *game::sv_maxclients; ++slot)
			{
				const auto& client = clients[slot];
				if (client.state != 5 || client.testClient || client.remoteAddress.type == game::NA_BOT)
				{
					continue;
				}
				const auto end = client.guid + strnlen(client.guid, sizeof(client.guid));
				std::uint64_t user{};
				const auto parsed = std::from_chars(client.guid, end, user, 16);
				if (end - client.guid <= 16 && parsed.ec == std::errc{} && parsed.ptr == end && user &&
					game::Party_FindMemberByXUID(party, user) == slot)
				{
					users.push_back(user);
				}
			}

			const auto match = utils::cryptography::random::get_challenge() + utils::cryptography::random::get_challenge();
			const auto awards = reward::distribute(users, sv_match_supply_drops->current.integer, match);
			for (const auto& award : awards)
			{
				const auto identity = demonware::runtime_context::get_snapshot();
				if (!game::environment::is_dedicated() && identity && identity->user_id == award.user)
				{
					// The listen host need not wait for loopback commands during shutdown.
					scheduler::once([award] { queue(award); }, scheduler::pipeline::main);
					continue;
				}
				const auto slot = game::Party_FindMemberByXUID(party, award.user);
				if (slot == UINT8_MAX || slot >= *game::sv_maxclients)
				{
					continue;
				}
				game::SV_SendServerCommand(&clients[slot], game::SV_CMD_RELIABLE, "%s %llx %s %u",
					reward::command, award.user, award.match.c_str(), award.quantity);
			}
		}

		const char* end_match()
		{
			try
			{
				award_drops();
			}
			catch (const std::exception& error)
			{
				console::error("Match rewards failed: %s\n", error.what());
			}
			return game::PartyHost_EndMatch();
		}
	}

	bool receive(const unsigned local_client, const command::params& args)
	{
		if (!args.size() || std::string_view{args[0]} != reward::command)
		{
			return false;
		}
		if (local_client || args.size() != 4 || game::environment::is_zombies())
		{
			return true;
		}

		const auto award = reward::parse(args[1], args[2], args[3]);
		const auto identity = demonware::runtime_context::get_snapshot();
		if (award && identity && identity->user_id == award->user)
		{
			// CG command delivery does not own the refresh/retry state. Keep all of
			// it on main, and capture the decoded value rather than command tokens.
			scheduler::once([value = *award] { queue(value); }, scheduler::pipeline::main);
		}
		return true;
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			sv_match_supply_drops = game::Dvar_RegisterInt("sv_matchSupplyDrops", 1, 0,
				reward::maximum_drops, game::DVAR_FLAG_NONE);

			// Scr_EndGame sets level's end flag to 3 before this call (0x5A1B09),
			// rejecting a second end in the same match. Hook only this call site:
			// disconnects, migration and server shutdown also call PartyHost_EndMatch.
			utils::hook::call(0x5A1B33_g, end_match);
		}
	};
}

REGISTER_COMPONENT(match_drops::component)
