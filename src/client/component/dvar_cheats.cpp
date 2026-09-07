#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "dvars.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>

namespace dvar_cheats
{
	namespace
	{
		void player_cmd_set_client_dvar(utils::hook::assembler& a)
		{
			const auto stock = a.new_label();

			// Entity, value and name validation already ran. RDI holds the name;
			// all live state is in nonvolatile registers or the native stack frame.
			a.mov(rcx, rdi);
			a.call_aligned(dvars::override::is_local);
			a.test(al, al);
			a.jz(stock);
			a.jmp(0x12E708_g); // Native epilogue, including the saved RBP restore.

			a.bind(stock);
			a.lea(r9, ptr(rsp, 0x450)); // Replaced instruction at 0x12E69C.
			a.jmp(0x12E6A4_g);
		}

		void player_cmd_set_client_dvars(utils::hook::assembler& a)
		{
			const auto stock = a.new_label();

			// Keep native argument/name validation and skip only this local dvar's
			// pair, preserving other player settings in the same batch.
			a.mov(rcx, rdi);
			a.call_aligned(dvars::override::is_local);
			a.test(al, al);
			a.jz(stock);
			a.jmp(0x12E971_g); // Advance to the next pair and retain the existing buffer.

			a.bind(stock);
			a.mov(rcx, rdi);
			a.call(0xAF9D0_g); // Replaced Dvar_FindMalleableVar thunk call at 0x12E82A.
			a.jmp(0x12E82F_g);
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			// Stock setclientdvar(s) would raise a script error for local dvars'
			// missing NETWORK flag/index, on listen and dedicated servers.
			utils::hook::nop(0x12E69C_g, 8);
			utils::hook::jump(0x12E69C_g, utils::hook::assemble(player_cmd_set_client_dvar));
			utils::hook::nop(0x12E827_g, 8);
			utils::hook::jump(0x12E827_g, utils::hook::assemble(player_cmd_set_client_dvars));
		}
	};
}

REGISTER_COMPONENT(dvar_cheats::component)
