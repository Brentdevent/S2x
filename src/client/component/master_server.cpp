#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "master_server.hpp"

#include "console/console.hpp"
#include "dedicated_party.hpp"
#include "network.hpp"
#include "scheduler.hpp"

#include <utils/string.hpp>

namespace master_server
{
	namespace
	{
		constexpr auto default_master_server = "127.0.0.1:20810";
		constexpr auto heartbeat_interval = 60s;
		constexpr auto retry_interval = 10s;

		game::dvar_t* master_server{};
		game::dvar_t* master_server_enable{};
		std::mutex address_cache_mutex{};
		std::string cached_address_string{};
		game::netadr_s cached_address{};
		bool cached_address_valid{};
		bool address_failure_logged{};
		std::uint64_t address_cache_generation{};
		std::chrono::steady_clock::time_point last_resolve_attempt{};
		bool party_was_joinable{};
		game::netadr_s last_heartbeat_target{};
		std::chrono::steady_clock::time_point last_heartbeat{};

		bool addresses_match(const game::netadr_s& lhs, const game::netadr_s& rhs)
		{
			return lhs.type == rhs.type && lhs.addr == rhs.addr && lhs.port == rhs.port;
		}

		bool get_cached_address(game::netadr_s& address)
		{
			std::lock_guard<std::mutex> lock{address_cache_mutex};
			if (!cached_address_valid)
			{
				return false;
			}

			address = cached_address;
			return true;
		}

		void clear_address_cache()
		{
			std::lock_guard<std::mutex> lock{address_cache_mutex};
			cached_address_valid = false;
			last_resolve_attempt = {};
			++address_cache_generation;
		}

		bool refresh_address_cache(game::netadr_s& address)
		{
			if (!is_enabled() || !master_server || !master_server->current.string)
			{
				clear_address_cache();
				return false;
			}

			auto value = std::string{master_server->current.string};
			utils::string::trim(value);

			const auto now = std::chrono::steady_clock::now();
			std::uint64_t generation{};
			{
				std::lock_guard<std::mutex> lock{address_cache_mutex};
				if (cached_address_string != value)
				{
					cached_address_string = value;
					cached_address_valid = false;
					address_failure_logged = false;
					last_resolve_attempt = {};
					++address_cache_generation;
				}

				if (cached_address_valid)
				{
					address = cached_address;
					return true;
				}

				if (last_resolve_attempt.time_since_epoch().count() != 0
					&& now - last_resolve_attempt < retry_interval)
				{
					return false;
				}

				last_resolve_attempt = now;
				generation = address_cache_generation;
			}

			game::netadr_s resolved{};
			const auto resolved_successfully = !value.empty()
				&& game::NET_StringToAdr(value.data(), &resolved)
				&& resolved.type > game::NA_BAD;

			if (resolved_successfully)
			{
				resolved.localNetID = game::NS_SERVER;
				resolved.addrHandleIndex = 0;
			}

			auto log_failure = false;
			{
				std::lock_guard<std::mutex> lock{address_cache_mutex};
				if (generation != address_cache_generation || cached_address_string != value)
				{
					if (cached_address_valid)
					{
						address = cached_address;
						return true;
					}

					return false;
				}

				cached_address_valid = resolved_successfully;
				if (resolved_successfully)
				{
					cached_address = resolved;
					address = resolved;
					address_failure_logged = false;
				}
				else if (!address_failure_logged)
				{
					address_failure_logged = true;
					log_failure = true;
				}
			}

			if (log_failure)
			{
				console::warn("[master_server] failed to resolve '%s'\n", value.data());
			}

			return resolved_successfully;
		}

		bool heartbeat_is_enabled()
		{
			if (!is_enabled())
			{
				return false;
			}

			const auto* sv_lan_only = game::Dvar_FindMalleableVar("sv_lanOnly");
			return !sv_lan_only || !sv_lan_only->current.enabled;
		}

		void run_heartbeat()
		{
			if (!game::environment::is_dedi() || !heartbeat_is_enabled())
			{
				party_was_joinable = false;
				return;
			}

			dedicated_party::connect_info connect_info{};
			if (!dedicated_party::get_connect_info(connect_info))
			{
				party_was_joinable = false;
				return;
			}

			const auto now = std::chrono::steady_clock::now();
			game::netadr_s target{};
			if (!get_address(target))
			{
				return;
			}

			if (party_was_joinable && addresses_match(target, last_heartbeat_target)
				&& now - last_heartbeat < heartbeat_interval)
			{
				return;
			}

			network::send(target, "heartbeat");
			console::info("[master_server] sent heartbeat to %s\n", network::net_adr_to_string(target));

			party_was_joinable = true;
			last_heartbeat_target = target;
			last_heartbeat = now;
		}
	}

	bool is_enabled()
	{
		return master_server_enable && master_server_enable->current.enabled;
	}

	bool get_address(game::netadr_s& address)
	{
		return refresh_address_cache(address);
	}

	bool is_master_address(const game::netadr_s& address)
	{
		if (!is_enabled())
		{
			return false;
		}

		game::netadr_s master{};
		return get_cached_address(master) && addresses_match(master, address);
	}

	bool handle_incoming_packet(
		const char* data,
		const int size,
		const std::uint32_t source_address,
		const std::uint16_t source_port)
	{
		constexpr auto packet_header_size = 4;

		if (!is_enabled() || !data || size <= packet_header_size)
		{
			return false;
		}

		game::netadr_s master{};
		if (!get_cached_address(master) || master.addr != source_address || master.port != source_port)
		{
			return false;
		}

		if (static_cast<std::uint8_t>(data[0]) != 0xFF
			|| static_cast<std::uint8_t>(data[1]) != 0xFF
			|| static_cast<std::uint8_t>(data[2]) != 0xFF
			|| static_cast<std::uint8_t>(data[3]) != 0xFF)
		{
			return false;
		}

		const std::string_view payload{data + packet_header_size, static_cast<std::size_t>(size - packet_header_size)};
		auto command_length = 0ull;
		while (command_length < payload.size()
			&& static_cast<unsigned char>(payload[command_length]) > ' ')
		{
			++command_length;
		}

		const auto command = utils::string::to_lower(std::string{payload.substr(0, command_length)});
		if (command != "getinfo" && command != "getserversresponse")
		{
			return false;
		}

		const auto payload_offset = packet_header_size + command_length
			+ static_cast<std::size_t>(command_length < payload.size());
		const std::string_view command_data{
			data + payload_offset,
			static_cast<std::size_t>(size) - payload_offset
		};

		console::debug("[master_server] dispatching standard %s packet from %s\n",
			command.data(), network::net_adr_to_string(master));

		network::dispatch(master, command, command_data);
		return true;
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			scheduler::once([]
			{
				master_server = game::Dvar_RegisterString(
					"master_server", default_master_server, game::DVAR_FLAG_SAVED);
				master_server_enable = game::Dvar_RegisterBool(
					"master_server_enable", true, game::DVAR_FLAG_SAVED);

				game::netadr_s address{};
				refresh_address_cache(address);
			}, scheduler::pipeline::main);

			if (game::environment::is_dedi())
			{
				scheduler::loop(run_heartbeat, scheduler::pipeline::main, 1s);
			}
		}
	};
}

REGISTER_COMPONENT(master_server::component)
