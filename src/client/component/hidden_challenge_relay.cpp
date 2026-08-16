#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "hidden_challenge_relay.hpp"

#include "command.hpp"
#include "console/console.hpp"
#include "hidden_challenges.hpp"
#include "scheduler.hpp"

#include "game/game.hpp"

#include <charconv>
#include <deque>
#include <mutex>

#include <utils/hook.hpp>

namespace hidden_challenge_relay
{
	namespace
	{
		constexpr std::string_view server_command = "s2x_hc";
		constexpr auto maximum_pending_forwards = 128u;
		constexpr auto minimum_command_client_state = 4;

		struct pending_forward
		{
			std::uint64_t user_id{};
			std::uint32_t group{};
			std::uint32_t challenge{};
		};

		utils::hook::detour deploy_server_command_hook;
		std::atomic_bool accepting_forwards{};
		std::mutex pending_forward_mutex{};
		std::deque<pending_forward> pending_forwards{};

		bool parse_unsigned(const char* text, std::uint32_t& value)
		{
			if (!text || !*text)
			{
				return false;
			}

			const auto* end = text + std::strlen(text);
			const auto result = std::from_chars(text, end, value);
			return result.ec == std::errc{} && result.ptr == end;
		}

		void deploy_server_command_stub(const unsigned int local_client_num)
		{
			// CG_DeployServerCommandString is entered with the reliable command
			// already tokenized by the stock client command path.
			const command::params params{};
			if (params.size() == 0 || std::string_view{params[0]} != server_command)
			{
				deploy_server_command_hook.invoke<void>(local_client_num);
				return;
			}

			std::uint32_t group{};
			std::uint32_t challenge{};
			if (params.size() != 3 || !parse_unsigned(params[1], group) ||
				!parse_unsigned(params[2], challenge))
			{
				console::debug("[hidden_challenges] ignored malformed server completion\n");
				return;
			}

			console::debug("[hidden_challenges] received server completion: group=%u slot=%u\n",
				group, challenge);
			hidden_challenges::submit_completion(group, challenge);
		}

		void process_pending_forwards()
		{
			if (!game::SV_Loaded())
			{
				return;
			}

			auto* party = game::Live_GetGameParty();
			auto* clients = *game::mp::svs_clients;
			const auto max_clients = *game::sv_maxclients;
			if (!party || !clients || max_clients <= 0)
			{
				return;
			}

			std::deque<pending_forward> forwards{};
			{
				std::lock_guard lock{pending_forward_mutex};
				forwards.swap(pending_forwards);
			}

			for (const auto& forward : forwards)
			{
				// The stock Achievement Engine sender resolves its XUID through this
				// party lookup and uses the returned member as the svs_clients index.
				const auto client_num = game::Party_FindMemberByXUID(party, forward.user_id);
				if (client_num == std::numeric_limits<std::uint8_t>::max() ||
					client_num >= max_clients || clients[client_num].state < minimum_command_client_state)
				{
					console::debug("[hidden_challenges] discarded completion for disconnected XUID %llu\n",
						static_cast<unsigned long long>(forward.user_id));
					continue;
				}

				console::debug(
					"[hidden_challenges] forwarding XUID %llu to client %u: group=%u slot=%u\n",
					static_cast<unsigned long long>(forward.user_id), client_num,
					forward.group, forward.challenge);
				game::SV_SendServerCommand(&clients[client_num], game::SV_CMD_RELIABLE,
					"%s %u %u", server_command.data(), forward.group, forward.challenge);
			}
		}

		void clear_pending_forwards()
		{
			std::lock_guard lock{pending_forward_mutex};
			pending_forwards.clear();
		}
	}

	void submit(const std::uint64_t user_id, const std::uint32_t group,
		const std::uint32_t challenge)
	{
		if (!accepting_forwards.load())
		{
			return;
		}

		std::lock_guard lock{pending_forward_mutex};
		if (!accepting_forwards.load())
		{
			return;
		}

		if (pending_forwards.size() >= maximum_pending_forwards)
		{
			console::debug("[hidden_challenges] pending forward queue is full\n");
			return;
		}

		pending_forwards.push_back({user_id, group, challenge});
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::is_zombies())
			{
				return;
			}

			accepting_forwards = true;
			scheduler::loop(process_pending_forwards, scheduler::pipeline::server);

			if (!game::environment::is_dedicated())
			{
				deploy_server_command_hook.create(game::CG_DeployServerCommandString,
					deploy_server_command_stub);
			}
		}

		void pre_destroy() override
		{
			accepting_forwards = false;
			clear_pending_forwards();
		}
	};
}

REGISTER_COMPONENT(hidden_challenge_relay::component)
