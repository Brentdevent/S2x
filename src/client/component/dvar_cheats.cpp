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
			a.jmp(game::select(0x12E708, 0x10E068)); // Native epilogue, including the saved RBP restore.

			a.bind(stock);
			a.lea(r9, ptr(rsp, 0x450)); // Replaced instruction at 0x12E69C.
			a.jmp(game::select(0x12E6A4, 0x10E004));
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
			a.jmp(game::select(0x12E971, 0x10E2D1)); // Advance to the next pair and retain the existing buffer.

			a.bind(stock);
			a.mov(rcx, rdi);
			a.call(game::select(0xAF9D0, 0x8F800)); // Replaced Dvar_FindMalleableVar thunk call at 0x12E82A.
			a.jmp(game::select(0x12E82F, 0x10E18F));
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			// Stock setclientdvar(s) would raise a script error for local dvars'
			// missing NETWORK flag/index, on listen and dedicated servers.
			utils::hook::nop(game::select(0x12E69C, 0x10DFFC), 8);
			utils::hook::jump(game::select(0x12E69C, 0x10DFFC), utils::hook::assemble(player_cmd_set_client_dvar));
			utils::hook::nop(game::select(0x12E827, 0x10E187), 8);
			utils::hook::jump(game::select(0x12E827, 0x10E187), utils::hook::assemble(player_cmd_set_client_dvars));
		}
	};
}

REGISTER_COMPONENT(dvar_cheats::component)
