#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/lookup/errors.hpp"

#include <utils/hook.hpp>

namespace error_messages
{
	namespace
	{
		utils::hook::detour com_error_hook;
		utils::hook::detour sys_error_hook;
		utils::hook::detour scr_object_error_hook;
		utils::hook::detour bg_anim_parse_error_hook;

		void* com_error_original = nullptr;
		void* sys_error_original = nullptr;
		void* scr_object_error_original = nullptr;
		void* bg_anim_parse_error_original = nullptr;

		const char* translate_format_stub(const char* fmt)
		{
			return fmt ? game::lookup::errors::translate_format(fmt).data() : fmt;
		}

		void* make_error_stub(const asmjit::x86::Gp& format_register, void** original)
		{
			return utils::hook::assemble([=](utils::hook::assembler& a)
			{
				// Preserve register and stack arguments, including variadic floating-point values.
				a.push(rcx);
				a.push(rdx);
				a.push(r8);
				a.push(r9);
				a.sub(rsp, 0x68); // Shadow space, XMM registers, and 16-byte call alignment.
				a.movdqu(xmmword_ptr(rsp, 0x20), xmm0);
				a.movdqu(xmmword_ptr(rsp, 0x30), xmm1);
				a.movdqu(xmmword_ptr(rsp, 0x40), xmm2);
				a.movdqu(xmmword_ptr(rsp, 0x50), xmm3);

				a.mov(rcx, format_register);
				a.call(translate_format_stub);

				a.movdqu(xmm0, xmmword_ptr(rsp, 0x20));
				a.movdqu(xmm1, xmmword_ptr(rsp, 0x30));
				a.movdqu(xmm2, xmmword_ptr(rsp, 0x40));
				a.movdqu(xmm3, xmmword_ptr(rsp, 0x50));
				a.add(rsp, 0x68);
				a.pop(r9);
				a.pop(r8);
				a.pop(rdx);
				a.pop(rcx);
				a.mov(format_register, rax);

				// Tail-jump so the engine retains its original formatting and longjmp behavior.
				a.mov(r11, reinterpret_cast<uint64_t>(original));
				a.jmp(qword_ptr(r11));
			});
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			// Initialize the lookup before entering an engine error path.
			(void)game::lookup::errors::translate_format("");

			com_error_hook.create(game::Com_Error, make_error_stub(rdx, &com_error_original));
			com_error_original = com_error_hook.get_original();
			sys_error_hook.create(game::Sys_Error, make_error_stub(rcx, &sys_error_original));
			sys_error_original = sys_error_hook.get_original();
			scr_object_error_hook.create(game::Scr_ObjectError, make_error_stub(rcx, &scr_object_error_original));
			scr_object_error_original = scr_object_error_hook.get_original();

			if (game::environment::uses_multiplayer_binary())
			{
				bg_anim_parse_error_hook.create(game::BG_AnimParseError, make_error_stub(rcx, &bg_anim_parse_error_original));
				bg_anim_parse_error_original = bg_anim_parse_error_hook.get_original();
			}
		}
	};
}

REGISTER_COMPONENT(error_messages::component)
