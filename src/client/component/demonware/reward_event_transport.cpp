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
#include <utils/string.hpp>

#include <charconv>
#include <deque>
#include <mutex>

namespace reward_event_relay
{
	namespace
	{
		namespace wire = demonware::reward_event_relay;

		constexpr auto connected_client_state = 3;
		constexpr auto ready_client_state = 4;
		constexpr auto reliable_command_budget = 64;
		constexpr auto retry_interval = 1s;
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
			std::uint64_t sent_sequence{};
			std::uint64_t acknowledged{};
			std::chrono::steady_clock::time_point retry_at{};
			bool running{};
			bool zombies{};
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
			return client.state >= connected_client_state && !client.testClient &&
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

		game::mp::client_t* find_client(const connection& peer)
		{
			auto* clients = *game::mp::svs_clients;
			if (!clients || peer.slot >= *game::sv_maxclients)
			{
				return nullptr;
			}

			auto& client = clients[peer.slot];
			if (!is_remote_client(client) || peer.guid != std::to_array(client.guid) ||
				peer.address != client.remoteAddress || peer.qport != client.qport ||
				peer.connected != client.lastConnectTime)
			{
				return nullptr;
			}

			return &client;
		}

		void send_batch(game::mp::client_t& client, const std::uint64_t user, event_stream& stream)
		{
			const auto now = std::chrono::steady_clock::now();
			if (client.state < ready_client_state || stream.pending.empty() || now < stream.retry_at ||
				client.reliableSequence - client.reliableAcknowledge >= reliable_command_budget)
			{
				return;
			}

			const auto count = std::min(stream.pending.size(), wire::batch_limit);
			const std::vector<wire::record> records{stream.pending.begin(), stream.pending.begin() + count};
			const auto payload = wire::encode(user, stream.id, stream.zombies, records);
			if (!payload.empty())
			{
				// Keep the batch until the client accepts it, even after native transport acknowledgement.
				game::SV_SendServerCommand(&client, game::SV_CMD_RELIABLE, "%s %s", wire::command, payload.c_str());
				stream.sent_sequence = records.back().sequence;
				stream.retry_at = now + retry_interval;
			}
		}

		bool flush()
		{
			std::lock_guard lock{mutex};

			// A full map load temporarily makes the server unavailable without disconnecting its clients.
			if (!game::SV_Loaded())
			{
				return false;
			}

			bool pending{};
			for (auto it = streams.begin(); it != streams.end();)
			{
				auto& stream = it->second;
				// The native connection survives a map load even while the party is being rebuilt.
				auto* client = find_client(stream.peer);
				if (!client ||
					stream.zombies != game::environment::is_zombies())
				{
					it = streams.erase(it);
					continue;
				}

				send_batch(*client, it->first, stream);

				pending |= !stream.pending.empty();
				++it;
			}

			flush_scheduled = pending;
			return !pending;
		}

		std::uint64_t enqueue(event_stream& stream, wire::record record)
		{
			// Every non-start record carries absolute elapsed time. Replace only an unsent
			// tail timer to preserve gameplay ordering and mission boundaries.
			if (record.type != wire::operation::start && !stream.pending.empty() &&
				stream.pending.back().sequence > stream.sent_sequence &&
				stream.pending.back().type == wire::operation::time &&
				record.seconds >= stream.pending.back().seconds)
			{
				record.sequence = stream.pending.back().sequence;
				stream.pending.back() = record;
				return record.sequence;
			}

			// Native slots provide backpressure within a map; copied records survive queue resets.
			record.sequence = ++stream.sequence;
			stream.pending.push_back(record);

			if (!flush_scheduled)
			{
				flush_scheduled = true;
				scheduler::schedule(flush, scheduler::pipeline::server);
			}

			return record.sequence;
		}

		event_stream* find_or_create_stream(const std::uint64_t user)
		{
			if (!accepting_events || !game::SV_Loaded())
			{
				return nullptr;
			}

			const auto found = streams.find(user);
			if (found != streams.end() && find_client(found->second.peer) &&
				found->second.zombies == game::environment::is_zombies())
			{
				return &found->second;
			}

			connection peer;
			if (!find_client(user, peer))
			{
				streams.erase(user);
				return nullptr;
			}

			auto& entry = streams[user];
			entry = {};
			entry.peer = peer;
			entry.zombies = game::environment::is_zombies();
			utils::cryptography::random::get_data(&entry.id, sizeof(entry.id));
			entry.id |= 1;

			return &entry;
		}

		bool read_number(const std::string_view text, std::uint64_t& value)
		{
			const auto end = text.data() + text.size();
			const auto parsed = std::from_chars(text.data(), end, value, 16);
			return !text.empty() && text.size() <= 16 && parsed.ec == std::errc{} && parsed.ptr == end;
		}

		void acknowledge(const int slot, const command::params_sv& args)
		{
			std::uint64_t id{}, sequence{};
			if (args.size() != 3 || !read_number(args[1], id) || !id || !read_number(args[2], sequence))
			{
				return;
			}

			std::lock_guard lock{mutex};
			if (!accepting_events)
			{
				return;
			}

			for (auto& [user, stream] : streams)
			{
				if (stream.id != id || stream.peer.slot != slot || sequence <= stream.acknowledged ||
					sequence > stream.sent_sequence)
				{
					continue;
				}

				if (!game::SV_Loaded() || !find_client(stream.peer) ||
					stream.zombies != game::environment::is_zombies())
				{
					return;
				}

				stream.acknowledged = sequence;
				while (!stream.pending.empty() && stream.pending.front().sequence <= sequence)
				{
					stream.pending.pop_front();
				}

				stream.retry_at = {};
				return;
			}
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
				const auto accepted = order_progress::receive(*payload);
				if (accepted)
				{
					const std::string text = utils::string::va("%s %llx %llx", wire::command, payload->stream, *accepted);
					const command::params tokens{text};
					game::CL_ForwardCommandToServer(0, text.c_str());
				}
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

	delivery admit(const std::uint64_t user, const demonware::order_progress::event& event, const int event_class)
	{
		if (event.id < 0 || event.count > maximum_event_count || event_class < 0 || event_class > 1)
		{
			return {};
		}

		std::lock_guard lock{mutex};

		auto* stream = find_or_create_stream(user);
		if (!stream)
		{
			return {};
		}

		stream->seconds = stream->running ? elapsed_seconds(*stream) : stream->seconds;
		const auto sequence = enqueue(*stream, {0, wire::operation::event, stream->seconds, static_cast<std::uint8_t>(event_class), event});
		return {stream->id, sequence};
	}

	bool is_pending(const std::uint64_t user, const delivery& value)
	{
		std::lock_guard lock{mutex};
		const auto found = streams.find(user);
		return found != streams.end() && found->second.id == value.stream &&
			value.sequence > found->second.acknowledged;
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
		if (!game::SV_Loaded())
		{
			return;
		}

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
		for (auto& [user, stream] : streams)
		{
			if (!stream.running)
			{
				continue;
			}

			// G_InitGame has already reset the clock. Close the old mission at its last
			// observed time, retaining delivery order and sequence numbers across maps.
			stream.running = false;
			enqueue(stream, {0, wire::operation::stop, stream.seconds});
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			accepting_events = true;
			command::add_sv(wire::command, acknowledge);

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
