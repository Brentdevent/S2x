#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "reward_event_transport.hpp"
#include "order_tracking.hpp"
#include "match_drops.hpp"
#include "commendations.hpp"
#include "component/command.hpp"
#include "component/network.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "game/demonware/runtime_context.hpp"

#include <utils/cryptography.hpp>
#include <utils/hook.hpp>

#include <deque>
#include <mutex>

namespace reward_event_relay
{
	namespace
	{
		namespace wire = demonware::reward_event_relay;

		constexpr auto minimum_client_state = 4;
		constexpr auto reliable_command_budget = 64;
		constexpr auto maximum_batches_per_frame = 4u;
		constexpr auto maximum_event_count = 10;

		struct connection
		{
			std::uint8_t slot{};
			std::array<char, 17> guid{};
			game::netadr_s address{};
			int qport{};
			int connected{};
		};

		struct event_stream
		{
			connection peer;
			std::uint64_t id{};
			std::uint64_t sequence{};
			bool running{};
			std::uint32_t start{};
			std::uint32_t seconds{};
			std::deque<wire::record> pending;
		};

		std::mutex mutex;
		std::unordered_map<std::uint64_t, event_stream> streams;
		bool flush_scheduled{};
		bool accepting_events{};

		utils::hook::detour deploy_hook;
		utils::hook::detour disconnect_hook;

		std::uint32_t match_clock()
		{
			return *reinterpret_cast<const std::uint32_t*>(0xA0E571C_g);
		}

		std::uint32_t elapsed_seconds(const event_stream& stream)
		{
			return (match_clock() - stream.start) / 1000;
		}

		bool is_local_user(const std::uint64_t user)
		{
			const auto identity = demonware::runtime_context::get_snapshot();
			return !game::environment::is_dedicated() && identity && identity->user_id == user;
		}

		bool is_remote_client(const game::mp::client_t& client)
		{
			return client.state >= minimum_client_state && !client.testClient &&
				client.remoteAddress.type != game::NA_BOT &&
				client.remoteAddress.type != game::NA_LOOPBACK;
		}

		game::mp::client_t* find_client(const std::uint64_t user, connection& peer)
		{
			if (!user || !game::SV_Loaded() || is_local_user(user))
			{
				return nullptr;
			}

			auto* party = game::Live_GetGameParty();
			auto* clients = *game::mp::svs_clients;
			if (!party || !clients)
			{
				return nullptr;
			}

			const auto slot = game::Party_FindMemberByXUID(party, user);
			if (slot == UINT8_MAX || slot >= *game::sv_maxclients)
			{
				return nullptr;
			}

			auto& client = clients[slot];
			if (!is_remote_client(client))
			{
				return nullptr;
			}

			peer = {
				slot,
				std::to_array(client.guid),
				client.remoteAddress,
				client.qport,
				client.lastConnectTime,
			};

			return &client;
		}

		bool same_connection(const connection& a, const connection& b)
		{
			return a.slot == b.slot && a.guid == b.guid && a.address == b.address &&
				a.qport == b.qport && a.connected == b.connected;
		}

		std::vector<wire::record> take_records(event_stream& stream)
		{
			std::vector<wire::record> records;
			while (records.size() < wire::batch_limit && !stream.pending.empty())
			{
				records.push_back(stream.pending.front());
				stream.pending.pop_front();
			}

			return records;
		}

		void send_batches(game::mp::client_t& client, const std::uint64_t user, event_stream& stream)
		{
			// Bounds each frame's use of the native 128-command reliable ring
			for (unsigned packets = 0; packets < maximum_batches_per_frame && !stream.pending.empty(); ++packets)
			{
				if (client.reliableSequence - client.reliableAcknowledge >= reliable_command_budget)
				{
					break;
				}

				const auto records = take_records(stream);
				const auto payload = wire::encode(user, stream.id, game::environment::is_zombies(), records);
				if (!payload.empty())
				{
					game::SV_SendServerCommand(&client, game::SV_CMD_RELIABLE, "%s %s", wire::command, payload.c_str());
				}
			}
		}

		bool flush()
		{
			std::lock_guard lock{mutex};

			bool pending{};
			for (auto it = streams.begin(); it != streams.end();)
			{
				connection peer;
				auto* client = find_client(it->first, peer);
				auto& stream = it->second;
				if (!client || !same_connection(peer, stream.peer))
				{
					it = streams.erase(it);
					continue;
				}

				send_batches(*client, it->first, stream);

				pending |= !stream.pending.empty();
				++it;
			}

			flush_scheduled = pending;
			return !pending;
		}

		void enqueue(event_stream& stream, wire::record record)
		{
			// Every non-start record carries absolute elapsed time. Replace only an unsent
			// tail timer to preserve gameplay ordering and mission boundaries.
			if (record.type != wire::operation::start && !stream.pending.empty() &&
				stream.pending.back().type == wire::operation::time &&
				record.seconds >= stream.pending.back().seconds)
			{
				record.sequence = stream.pending.back().sequence;
				stream.pending.back() = record;
				return;
			}

			if (stream.pending.size() >= wire::pending_limit)
			{
				return;
			}

			record.sequence = ++stream.sequence;
			stream.pending.push_back(record);

			if (!flush_scheduled)
			{
				flush_scheduled = true;
				scheduler::schedule(flush, scheduler::pipeline::server);
			}
		}

		event_stream* find_or_create_stream(const std::uint64_t user)
		{
			if (!accepting_events)
			{
				return nullptr;
			}

			connection peer;
			if (!find_client(user, peer))
			{
				streams.erase(user);
				return nullptr;
			}

			auto& entry = streams[user];
			if (!entry.id || !same_connection(peer, entry.peer))
			{
				entry = {};
				entry.peer = peer;
				utils::cryptography::random::get_data(&entry.id, sizeof(entry.id));
				entry.id |= 1;
			}

			return &entry;
		}

		void deploy(const unsigned int local_client)
		{
			const command::params args;
			if (match_drops::receive(local_client, args) || commendations::receive(local_client, args))
			{
				return;
			}

			if (!args.size() || std::string_view{args[0]} != wire::command)
			{
				deploy_hook.invoke<void>(local_client);
				return;
			}

			if (local_client || args.size() != 2)
			{
				return;
			}

			const auto payload = wire::decode(args[1]);
			if (payload)
			{
				order_progress::receive(*payload);
			}
		}

		void disconnect(const int local_client)
		{
			if (!local_client)
			{
				commendations::disconnect();
				order_progress::disconnect();
			}

			disconnect_hook.invoke<void>(local_client);
		}
	}

	void admit(const std::uint64_t user, const demonware::order_progress::event& event, const int event_class)
	{
		if (event.id < 0 || event.count > maximum_event_count || event_class < 0 || event_class > 1)
		{
			return;
		}

		std::lock_guard lock{mutex};

		auto* stream = find_or_create_stream(user);
		if (!stream)
		{
			return;
		}

		const auto seconds = stream->running ? elapsed_seconds(*stream) : stream->seconds;
		enqueue(*stream, {0, wire::operation::event, seconds, static_cast<std::uint8_t>(event_class), event});
	}

	void start(const std::uint64_t user)
	{
		std::lock_guard lock{mutex};

		auto* stream = find_or_create_stream(user);
		if (!stream)
		{
			return;
		}

		stream->running = true;
		stream->start = match_clock();
		stream->seconds = 0;
		enqueue(*stream, {0, wire::operation::start});
	}

	void stop(const std::uint64_t user)
	{
		std::lock_guard lock{mutex};

		const auto it = streams.find(user);
		if (it == streams.end() || !it->second.running)
		{
			return;
		}

		auto& stream = it->second;
		stream.seconds = elapsed_seconds(stream);
		stream.running = false;
		enqueue(stream, {0, wire::operation::stop, stream.seconds});
	}

	void tick()
	{
		std::lock_guard lock{mutex};

		for (auto& [user, stream] : streams)
		{
			if (!stream.running)
			{
				continue;
			}

			const auto seconds = elapsed_seconds(stream);
			if (seconds == stream.seconds)
			{
				continue;
			}

			stream.seconds = seconds;
			enqueue(stream, {0, wire::operation::time, seconds});
		}
	}

	void reset()
	{
		std::lock_guard lock{mutex};
		streams.clear();
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			accepting_events = true;

			if (game::environment::is_dedicated())
			{
				return;
			}

			// '$' has no stock CG_DeployServerCommandString case, so older clients ignore it
			deploy_hook.create(game::CG_DeployServerCommandString, deploy);

			// CL_Disconnect also covers errors and timeouts
			disconnect_hook.create(0x6DC50_g, disconnect);
		}

		void pre_destroy() override
		{
			std::lock_guard lock{mutex};
			accepting_events = false;
			streams.clear();
		}
	};
}

REGISTER_COMPONENT(reward_event_relay::component)
