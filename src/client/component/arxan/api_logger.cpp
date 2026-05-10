#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace arxan::api_logger
{
	namespace
	{
		struct call_context
		{
			uintptr_t target;          // RAX = precalculated function address we're jumping to
			uintptr_t return_address;  // where the function will RET to (original caller)
			uintptr_t param1;          // RCX
			uintptr_t param2;          // RDX
			uintptr_t param3;          // R8
			uintptr_t param4;          // R9
		};

		auto mutex = std::mutex();

		void log_api_call(call_context* ctx)
		{
			if (!ctx)
			{
				return;
			}

			const auto game = utils::nt::library{};
			const auto d3d11 = utils::nt::library("d3d11.dll");
			const auto source_module = utils::nt::library::get_by_address((void*)ctx->target);

			if (source_module == game || source_module == d3d11)
			{
				return;
			}


			thread_local bool in_logger = false;
			if (in_logger)
			{
				return;
			}

			in_logger = true;
			std::lock_guard lock(mutex);

			OutputDebugStringA(utils::string::va(
				"[LOG] Jumping to 0x%llX (return to 0x%llX) | RCX=0x%llX RDX=0x%llX R8=0x%llX R9=0x%llX\n",
				ctx->target,
				ctx->return_address,
				ctx->param1,
				ctx->param2,
				ctx->param3,
				ctx->param4));

			const auto target_mod = utils::nt::library::get_by_address((void*)ctx->target);
			if (target_mod)
			{
				ptrdiff_t offset{};
				auto name = target_mod.get_nearest_export_name_by_address((void*)ctx->target, &offset);

				if (!name.empty())
				{
					OutputDebugStringA(utils::string::va(
						"          -> Target: %s!%s+0x%llX\n",
						target_mod.get_name().data(),
						name.data(),
						offset));
				}
				else
				{
					OutputDebugStringA(utils::string::va(
						"          -> Target: %s\n",
						target_mod.get_name().data()));
				}
			}

			const auto ret_mod = utils::nt::library::get_by_address((void*)ctx->return_address);
			if (ret_mod)
			{
				ptrdiff_t offset{};
				auto name = ret_mod.get_nearest_export_name_by_address((void*)ctx->return_address, &offset);

				if (!name.empty())
				{
					OutputDebugStringA(utils::string::va(
						"          -> Return: %s!%s+0x%llX\n",
						ret_mod.get_name().data(),
						name.data(),
						offset));
				}
				else
				{
					OutputDebugStringA(utils::string::va(
						"          -> Return: %s\n",
						ret_mod.get_name().data()));
				}
			}

			in_logger = false;
		}

		void thunk_stub(utils::hook::assembler& a)
		{
			a.pushad64();

			// Reserve:
			// 0x30 = call_context
			// 0x60 = xmm0..xmm5
			// total = 0x90
			a.sub(rsp, 0x90);

			// Save XMM0..XMM5
			a.movdqu(xmmword_ptr(rsp, 0x30), xmm0);
			a.movdqu(xmmword_ptr(rsp, 0x40), xmm1);
			a.movdqu(xmmword_ptr(rsp, 0x50), xmm2);
			a.movdqu(xmmword_ptr(rsp, 0x60), xmm3);
			a.movdqu(xmmword_ptr(rsp, 0x70), xmm4);
			a.movdqu(xmmword_ptr(rsp, 0x80), xmm5);

			// Build call_context
			// After pushad64() = 0x78 bytes
			// After sub rsp, 0x90, original return address is at [rsp + 0x108]

			a.mov(qword_ptr(rsp, 0x00), rax); // target
			a.mov(r11, qword_ptr(rsp, 0x108));
			a.mov(qword_ptr(rsp, 0x08), r11); // return_address
			a.mov(qword_ptr(rsp, 0x10), rcx); // param1
			a.mov(qword_ptr(rsp, 0x18), rdx); // param2
			a.mov(qword_ptr(rsp, 0x20), r8);  // param3
			a.mov(qword_ptr(rsp, 0x28), r9);  // param4

			// RCX = &call_context
			a.mov(rcx, rsp);

			// Windows x64 shadow space
			// With fixed pushad64, 0x20 is enough here
			a.sub(rsp, 0x20);
			a.call(log_api_call);
			a.add(rsp, 0x20);

			// Restore XMM0..XMM5
			a.movdqu(xmm0, xmmword_ptr(rsp, 0x30));
			a.movdqu(xmm1, xmmword_ptr(rsp, 0x40));
			a.movdqu(xmm2, xmmword_ptr(rsp, 0x50));
			a.movdqu(xmm3, xmmword_ptr(rsp, 0x60));
			a.movdqu(xmm4, xmmword_ptr(rsp, 0x70));
			a.movdqu(xmm5, xmmword_ptr(rsp, 0x80));

			// Free locals
			a.add(rsp, 0x90);
			a.popad64();

			a.jmp(rax);
		}
	}

	struct component final : generic_component
	{
	public:
		void post_load() override
		{
			utils::hook::set(game::select(0xB1C688, 0x84D518), utils::hook::assemble(thunk_stub));
		}

		component_priority priority() const override
		{
			return component_priority::arxan;
		}
	};
}

#ifdef ARXAN_DEBUG
REGISTER_COMPONENT(arxan::api_logger::component)
#endif
