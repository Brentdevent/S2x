#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "breakpoint_locations.hpp"
#include "game/game.hpp"

#include <utils/hook.hpp>

#define PRECOMPUTED

namespace arxan::breakpoints
{
	namespace
	{
		std::mutex handler_mutex;
		std::vector<std::pair<PVOID, void*>> handlers;

		void fake_exception(void* address, _CONTEXT* fake_context, DWORD exception)
		{
			// Guards run on multiple threads while others register/remove handlers, so iterate a snapshot
			std::vector<std::pair<PVOID, void*>> handlers_copy;
			{
				std::lock_guard _(handler_mutex);
				handlers_copy = handlers;
			}

			_EXCEPTION_POINTERS fake_info{};
			_EXCEPTION_RECORD fake_record{};
			fake_info.ExceptionRecord = &fake_record;
			fake_info.ContextRecord = fake_context;

			fake_record.ExceptionAddress = reinterpret_cast<void*>(reinterpret_cast<std::uint64_t>(address) + 3);
			fake_record.ExceptionCode = exception;

			for (const auto& handler : handlers_copy)
			{
				auto result = utils::hook::invoke<LONG>(handler.second, &fake_info);
				if (result)
				{
					memset(fake_context, 0, sizeof(_CONTEXT));
					break;
				}
			}
		}

		void patch_int2d_trap(void* address)
		{
			const auto game_address = reinterpret_cast<std::uint64_t>(address);
			const auto jump_target = utils::hook::extract<void*>(reinterpret_cast<void*>(game_address + 3));

			_CONTEXT* fake_context = new _CONTEXT{};
			const auto stub = utils::hook::assemble([address, jump_target, fake_context](utils::hook::assembler& a)
			{
				a.push(rcx);
				a.mov(rcx, fake_context);
				a.call_aligned(RtlCaptureContext);
				a.pop(rcx);

				a.pushad64();
				a.mov(rcx, address);
				a.mov(rdx, fake_context);
				a.mov(r8, EXCEPTION_BREAKPOINT);
				a.call_aligned(fake_exception);
				a.popad64();

				a.jmp(jump_target);
			});

			// The stub jumps to the original target, so the trailing two bytes are never executed.
			// Avoid leaving a NOP-only window before installing the jump.
			utils::hook::jump(game_address, stub, false);
		}

		void patch_breakpoints_precomputed()
		{
			if (game::environment::uses_multiplayer_binary())
			{
				for (const auto offset : mp::int2d_breakpoint_offsets)
				{
					patch_int2d_trap(reinterpret_cast<void*>(game::relocate(offset)));
				}
			}
			else
			{
				for (const auto offset : sp::int2d_breakpoint_offsets)
				{
					patch_int2d_trap(reinterpret_cast<void*>(game::relocate(offset)));
				}
			}
		}

		void search_and_patch_breakpoints()
		{
			const auto int2d_results = "CD 2D E9 ? ? ? ?"_sig;
			for (auto* i : int2d_results)
			{
				patch_int2d_trap(i);
			}
		}

		std::once_flag patch_once;

		void patch_breakpoints()
		{
			std::call_once(patch_once, []
			{
#ifdef PRECOMPUTED
				patch_breakpoints_precomputed();
#else
				search_and_patch_breakpoints();
#endif
			});
		}

		PVOID WINAPI add_vectored_exception_handler_stub(ULONG first, PVECTORED_EXCEPTION_HANDLER handler)
		{
			patch_breakpoints();

			auto handle = AddVectoredExceptionHandler(first, handler);
			if (handle)
			{
				std::lock_guard _(handler_mutex);
				handlers.emplace_back(handle, handler);
			}

			return handle;
		}

		ULONG WINAPI remove_vectored_exception_handler_stub(PVOID handle)
		{
			{
				std::lock_guard _(handler_mutex);
				std::erase_if(handlers, [handle](const auto& entry) { return entry.first == handle; });
			}

			return RemoveVectoredExceptionHandler(handle);
		}
	}

	struct component final : generic_component
	{
	public:
		void post_load() override
		{
			auto* add_vectored_exception_handler_import = utils::nt::library{}.get_iat_entry("kernel32.dll", "AddVectoredExceptionHandler");
			if (add_vectored_exception_handler_import)
			{
				utils::hook::set(add_vectored_exception_handler_import, add_vectored_exception_handler_stub);
			}

			auto* remove_vectored_exception_handler_import = utils::nt::library{}.get_iat_entry("kernel32.dll", "RemoveVectoredExceptionHandler");
			if (remove_vectored_exception_handler_import)
			{
				utils::hook::set(remove_vectored_exception_handler_import, remove_vectored_exception_handler_stub);
			}
		}

		component_priority priority() const override
		{
			return component_priority::arxan;
		}
	};
}

REGISTER_COMPONENT(arxan::breakpoints::component)
