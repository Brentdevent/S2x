#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "network.hpp"

#include "game/game.hpp"

#include "console/console.hpp"
#include "party.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace network
{
	namespace
	{
		utils::hook::detour sys_send_packet_hook;
		utils::hook::detour cl_dispatch_connectionless_packet_hook;
		utils::hook::detour net_compare_adr_hook;

		bool is_raw_network_address(const game::netadr_s* address)
		{
			if (!address || game::is_local_play())
			{
				return false;
			}

			const auto type = address->type;
			const auto invalid_handle_index = *reinterpret_cast<const std::uint32_t*>(0xF9D514_g);
			return (type == game::NA_IP || type == game::NA_BROADCAST)
				&& address->addrHandleIndex == invalid_handle_index;
		}

		int net_compare_adr_stub(const game::netadr_s* a, const game::netadr_s* b)
		{
			if (!is_raw_network_address(a) || !is_raw_network_address(b))
			{
				return net_compare_adr_hook.invoke<int>(a, b);
			}

			if (a->type != b->type)
			{
				return false;
			}

			if (a->localNetID != b->localNetID
				&& a->localNetID != game::NS_SERVER
				&& b->localNetID != game::NS_SERVER)
			{
				return false;
			}

			return a->addr == b->addr && a->port == b->port;
		}

		void enable_raw_reliable_messages()
		{
			const auto continue_scan = 0x76E882_g;
			const auto reject_address = 0x76E93F_g;
			const auto admission_stub = utils::hook::assemble([continue_scan, reject_address](utils::hook::assembler& a)
			{
				const auto admit = a.new_label();
				const auto reject = a.new_label();

				// Preserve the stock secure-handle path. Only raw S2x addresses may bypass it.
				a.jnz(admit);
				a.mov(rcx, r15);
				a.call_aligned(is_raw_network_address);
				a.test(al, al);
				a.jz(reject);

				a.bind(admit);
				a.jmp(reinterpret_cast<void*>(continue_scan));

				a.bind(reject);
				a.jmp(reinterpret_cast<void*>(reject_address));
			});

			utils::hook::nop(0x76E87C_g, 6);
			utils::hook::jump(0x76E87C_g, admission_stub);
		}

		void preserve_raw_session_addresses()
		{
			const auto preserve_address = 0x82821E_g;
			const auto convert_to_loopback = 0x828208_g;
			const auto get_invalid_handle_index = 0x800720_g;
			const auto stub = utils::hook::assemble(
				[preserve_address, convert_to_loopback, get_invalid_handle_index](utils::hook::assembler& a)
				{
					const auto preserve = a.new_label();

					// Stock S2 treats an invalid secure handle as a local peer. Raw S2x
					// UDP peers use that marker too, but must retain their IP endpoint.
					a.mov(rcx, rbx);
					a.call_aligned(is_raw_network_address);
					a.test(al, al);
					a.jnz(preserve);

					a.call_aligned(reinterpret_cast<void*>(get_invalid_handle_index));
					a.cmp(eax, dword_ptr(rbx, 0x10));
					a.jne(preserve);
					a.cmp(dword_ptr(rbx), 2);
					a.je(preserve);
					a.jmp(reinterpret_cast<void*>(convert_to_loopback));

					a.bind(preserve);
					a.jmp(reinterpret_cast<void*>(preserve_address));
				});

			utils::hook::nop(0x8281F9_g, 15);
			utils::hook::jump(0x8281F9_g, stub);
		}

		std::unordered_map<std::string, std::vector<callback>>& get_callbacks()
		{
			static std::unordered_map<std::string, std::vector<callback>> callbacks{};
			return callbacks;
		}

		bool handle_command(game::netadr_s* address, const char* command, game::msg_t* message)
		{
			if (!address || !command || !message || !message->data)
			{
				return false;
			}

			const auto command_string = utils::string::to_lower(command);
			const auto& callbacks = get_callbacks();

			const auto handler = callbacks.find(command_string);
			if (handler == callbacks.end())
			{
				return false;
			}

			// OOB packet format:
			// FF FF FF FF <command> <separator> <payload>
			const auto payload_offset = command_string.size() + 5;

			if (message->cursize < 0 || static_cast<std::size_t>(message->cursize) < payload_offset)
			{
				return false;
			}

			const std::string_view payload(
				message->data + payload_offset,
				static_cast<std::size_t>(message->cursize) - payload_offset
			);

			for (const auto& callback : handler->second)
			{
				try
				{
					console::debug(
						"[network] handling OOB command \"%s\" from %s\n",
						command_string.data(),
						net_adr_to_string(*address)
					);

					callback(*address, payload);
				}
				catch (const std::exception& e)
				{
					console::error("[network] command \"%s\" failed: %s\n", command_string.data(), e.what());
				}
				catch (...)
				{
					console::error("[network] command \"%s\" failed with unknown exception\n", command_string.data());
				}
			}

			return true;
		}

		bool cl_dispatch_connectionless_packet_stub(
			const int local_client_num,
			game::netadr_s* from,
			game::msg_t* message,
			const int time)
		{
			const auto command = game::Cmd_Argv(0);

			console::debug(
				"[network] OOB cmd=\"%s\" from=%s size=%i read=%i\n",
				command,
				from ? network::net_adr_to_string(*from) : "null",
				message ? message->cursize : -1,
				message ? message->readcount : -1
			);

			const std::string_view command_view = command ? command : "";
			if (command_view.ends_with("pa_joinfailed") && message && message->data
				&& message->readcount >= 0 && message->readcount + 4 <= message->cursize)
			{
				std::uint32_t reason{};
				std::memcpy(&reason, message->data + message->readcount, sizeof(reason));
				console::error("[network] Hosted party join rejected with reason %u.\n", reason);
			}

			if (handle_command(from, command, message))
			{
				return true;
			}

			return cl_dispatch_connectionless_packet_hook.invoke<bool>(local_client_num, from, message, time);
		}

		int sys_send_packet_stub(const game::netsrc_t sock, const int length, const char* data, const game::netadr_s* to)
		{
			if (!data || !to || length < 0)
			{
				return 0;
			}

			if (game::is_local_play())
			{
				return sys_send_packet_hook.invoke<int>(sock, length, data, to);
			}

			if (to->type != game::NA_IP && to->type != game::NA_BROADCAST)
			{
				console::debug(
					"[network] Sys_SendPacket: unsupported address type %i\n",
					static_cast<int>(to->type)
				);

				return 0;
			}

			const auto socket = *game::ip_socket;
			if (!socket)
			{
				return 0;
			}

			constexpr std::size_t max_packet_payload_size = 0x4EE; // 1262 bytes
			constexpr std::size_t packet_trailer_size = 3;

			const auto payload_size = static_cast<std::size_t>(length);

			if (payload_size > max_packet_payload_size)
			{
				console::warn(
					"[network] Sys_SendPacket: packet too large: payload=%zu max=%zu\n",
					payload_size,
					max_packet_payload_size
				);

				return 0;
			}

			std::vector<char> buffer(payload_size + packet_trailer_size);
	
			const auto checksum = game::Sys_ChecksumCopy(buffer.data(), data, static_cast<int>(payload_size));
			buffer[payload_size + 0] = static_cast<char>((checksum >> 8) & 0xFF);
			buffer[payload_size + 1] = static_cast<char>(checksum & 0xFF);
			buffer[payload_size + 2] = static_cast<char>(
				(static_cast<int>(sock) & 0xF) |
				((static_cast<int>(to->localNetID) & 0xF) << 4)
			);

			sockaddr_in address{};
			address.sin_family = AF_INET;
			address.sin_port = to->port;
			std::memcpy(&address.sin_addr.s_addr, to->ip, 4);

			const auto result = sendto(
				socket,
				buffer.data(),
				static_cast<int>(buffer.size()),
				0,
				reinterpret_cast<sockaddr*>(&address),
				sizeof(address)
			);

			if (result == SOCKET_ERROR)
			{
				console::warn(
					"[network] sendto failed: %s\n",
					std::system_category().message(WSAGetLastError()).data()
				);

				return 0;
			}

			return result;
		}

		bool is_current_server(const game::netadr_s& address)
		{
			return game::NET_CompareBaseAdr(&address, &party::get_target());
		}

		void reinstate_custom_print_oob()
		{
			on("print", [](const game::netadr_s& address, const std::string_view& message)
			{
				if (!is_current_server(address))
				{
					return;
				}

				console::info("%s", message.data());
			});
		}
	}

	void on(const std::string& command, const callback& callback)
	{
		get_callbacks()[utils::string::to_lower(command)].push_back(callback);
	}

	void send(const game::netadr_s& address, const std::string& command, const std::string& data, const char separator)
	{
		console::debug(
			"[network] sending OOB cmd=\"%s\" to=%s type=%i localNetID=%i len=%zu\n",
			command.data(),
			net_adr_to_string(address),
			static_cast<int>(address.type),
			static_cast<int>(address.localNetID),
			data.size()
		);

		std::string packet = "\xFF\xFF\xFF\xFF";
		packet.append(command);
		packet.push_back(separator);
		packet.append(data);

		send_data(address, packet);
	}

	void send_data(const game::netadr_s& address, const std::string_view& data)
	{
		if (data.empty())
		{
			return;
		}

		if (address.type <= game::NA_BAD)
		{
			return;
		}

		game::NET_SendPacket(
			game::NS_CLIENT1,
			static_cast<int>(data.size()),
			data.data(),
			&address
		);
	}

	bool are_addresses_equal(const game::netadr_s& a, const game::netadr_s& b)
	{
		if (a.type != b.type)
		{
			return false;
		}

		return a.port == b.port && a.addr == b.addr;
	}

	const char* net_adr_to_string(const game::netadr_s& address)
	{
		if (address.type == game::NA_LOOPBACK)
		{
			return "loopback";
		}

		if (address.type == game::NA_BOT)
		{
			return "bot";
		}

		if (address.type == game::NA_IP || address.type == game::NA_BROADCAST)
		{
			if (address.port)
			{
				return utils::string::va(
					"%u.%u.%u.%u:%u",
					address.ip[0],
					address.ip[1],
					address.ip[2],
					address.ip[3],
					ntohs(address.port)
				);
			}

			return utils::string::va(
				"%u.%u.%u.%u",
				address.ip[0],
				address.ip[1],
				address.ip[2],
				address.ip[3]
			);
		}

		return "bad";
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			net_compare_adr_hook.create(game::NET_CompareAdr, net_compare_adr_stub);
			enable_raw_reliable_messages();
			preserve_raw_session_addresses();

			cl_dispatch_connectionless_packet_hook.create(
				game::CL_DispatchConnectionlessPacket, cl_dispatch_connectionless_packet_stub
			);

			sys_send_packet_hook.create(game::Sys_SendPacket, sys_send_packet_stub);

			// Handle xuid without secure connection
			utils::hook::nop(0x6DBDC0_g, 6);

			// Don't establish secure connection
			utils::hook::set<uint8_t>(0x49706C_g, 0xEB);
			utils::hook::jump(0x6AFE3_g, 0x6B07D_g, true);

			// Skip onlinegame check EXE_ERR_UNREGISTERED_CONNECTION
			utils::hook::set<uint8_t>(0xF39E4_g, 0xEB);

			// Skip another onlinegame check (drops client EXE_TRANSMITERROR)
			utils::hook::jump(0xF739E_g, 0xF73F7_g);

			// Disable built in "print" OOB command
			utils::hook::set<std::uint8_t>(0x7023F_g, 0xEB);

			if (!game::environment::is_dedi())
			{
				reinstate_custom_print_oob();
			}
		}
	};
}

REGISTER_COMPONENT(network::component)
