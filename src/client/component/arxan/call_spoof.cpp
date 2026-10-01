#include <std_include.hpp>

#include "game/game.hpp"

#include <utils/hook.hpp>

#include <stack>

namespace arxan::detail
{
	void* callstack_proxy_addr{ nullptr };
	thread_local const void* address_to_call{};

	void set_address_to_call(const void* address)
	{
		address_to_call = address;
	}
}

namespace arxan::call_spoof
{
	namespace
	{
		thread_local std::stack<uint64_t> address_stack{};

		const void* get_address_to_call()
		{
			return arxan::detail::address_to_call;
		}

		void store_address(const uint64_t address)
		{
			address_stack.push(address);
		}

		uint64_t get_stored_address()
		{
			const auto res = address_stack.top();
			address_stack.pop();

			return res;
		}

		void callstack_return_stub(utils::hook::assembler& a)
		{
			a.push(rax);
			a.pushad64();

			a.call_aligned(get_stored_address);
			a.mov(qword_ptr(rsp, 0x78), rax);

			a.popad64();

			a.add(rsp, 8);

			a.jmp(qword_ptr(rsp, -8));
		}

		uint64_t get_callstack_return_stub()
		{
			static const auto stub = []()
			{
				const auto placeholder = game::select(0x9FFD79, 0x76F2F5);

				utils::hook::set<uint8_t>(placeholder - 2, 0xFF);
				utils::hook::nop(placeholder, 1);
				utils::hook::jump(placeholder + 1, utils::hook::assemble(callstack_return_stub));

				return placeholder;
			}();

			return stub;
		}

		void callstack_stub(utils::hook::assembler& a)
		{
			a.push(rax);

			a.pushad64();
			a.call_aligned(get_address_to_call);
			a.mov(qword_ptr(rsp, 0x78), rax);

			a.mov(rcx, qword_ptr(rsp, 0x80));
			a.call_aligned(store_address);

			a.mov(rax, get_callstack_return_stub());
			a.mov(qword_ptr(rsp, 0x80), rax);

			a.popad64();

			a.add(rsp, 8);

			a.jmp(qword_ptr(rsp, -8));
		}
	}

	struct component final : generic_component
	{
	public:
		static constexpr bool steam_binary_only = true;

		void post_unpack() override
		{
			arxan::detail::callstack_proxy_addr = utils::hook::assemble(callstack_stub);
		}
	};
}

REGISTER_COMPONENT(arxan::call_spoof::component)
