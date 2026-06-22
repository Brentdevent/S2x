#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "integrity_locations.hpp"
#include "game/game.hpp"

#include "utils/hook.hpp"
#include "utils/string.hpp"

#define PRECOMPUTED

namespace arxan::integrity
{
	namespace
	{
		const std::vector<std::pair<uint8_t*, size_t>>& get_text_sections()
		{
			static const std::vector<std::pair<uint8_t*, size_t>> text = []
			{
				std::vector<std::pair<uint8_t*, size_t>> texts{};

				const utils::nt::library game{};
				for (const auto& section : game.get_section_headers())
				{
					if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE)
					{
						texts.emplace_back(game.get_ptr() + section->VirtualAddress, section->Misc.VirtualSize);
					}
				}

				return texts;
			}();

			return text;
		}

		bool is_in_texts(const uint64_t addr)
		{
			const auto& texts = get_text_sections();
			for (const auto& text : texts)
			{
				const auto start = reinterpret_cast<ULONG_PTR>(text.first);
				if (addr >= start && addr <= (start + text.second))
				{
					return true;
				}
			}

			return false;
		}

		bool is_in_texts(const void* addr)
		{
			return is_in_texts(reinterpret_cast<uint64_t>(addr));
		}

		struct integrity_handler_context
		{
			uint32_t* computed_checksum;
			uint32_t* original_checksum;
		};

		bool is_on_stack(uint8_t* stack_frame, const void* pointer)
		{
			const auto stack_value = reinterpret_cast<uint64_t>(stack_frame);
			const auto pointer_value = reinterpret_cast<uint64_t>(pointer);

			const auto diff = static_cast<int64_t>(stack_value - pointer_value);
			return std::abs(diff) < 0x1000;
		}

		bool is_handler_context(uint8_t* stack_frame, const uint32_t computed_checksum, const uint32_t frame_offset, const uint32_t index)
		{
			const auto* potential_context = reinterpret_cast<integrity_handler_context*>(stack_frame + frame_offset);

			return is_on_stack(stack_frame, &potential_context->computed_checksum[index])
				&& potential_context->computed_checksum[index] == computed_checksum
				&& is_in_texts(&potential_context->original_checksum[index]);
		}

		integrity_handler_context* search_handler_context(uint8_t* stack_frame, const uint32_t computed_checksum, const uint32_t index)
		{
			for (uint32_t frame_offset = 0; frame_offset < 0x140; frame_offset += 8)
			{
				if (is_handler_context(stack_frame, computed_checksum, frame_offset, index))
				{
					return reinterpret_cast<integrity_handler_context*>(stack_frame + frame_offset);
				}
			}

			return nullptr;
		}

		uint32_t adjust_integrity_checksum(const uint64_t return_address, uint8_t* stack_frame, const uint32_t current_checksum, const uint32_t index)
		{
			const auto handler_address = game::derelocate(return_address - 5);
			const auto* context = search_handler_context(stack_frame, current_checksum, index);

			if (!context)
			{
				MessageBoxA(nullptr, utils::string::va("No frame offset for: %llX", handler_address), "Error",
					MB_ICONERROR);
				TerminateProcess(GetCurrentProcess(), 0xBAD);
				return current_checksum;
			}

			const auto correct_checksum = context->original_checksum[index];
			context->computed_checksum[index] = correct_checksum;

#ifdef ARXAN_DEBUG
			if (current_checksum != correct_checksum)
			{
				OutputDebugStringA(utils::string::va("Adjusting checksum (%llX): %X -> %X\n", handler_address,
					current_checksum, correct_checksum));
			}
#endif

			return correct_checksum;
		}

		void patch_intact_basic_block_integrity_check(void* address)
		{
			const auto game_address = reinterpret_cast<uint64_t>(address);
			constexpr auto inst_len = 3;

			const auto next_inst_addr = game_address + inst_len;
			const auto next_inst = *reinterpret_cast<uint32_t*>(next_inst_addr);

			if ((next_inst & 0xFF00FFFF) != 0xFF004583)
			{
				throw std::runtime_error(utils::string::va("Unable to patch intact basic block: %llX", game::derelocate(game_address)));
			}

			const auto other_frame_offset = static_cast<uint8_t>(next_inst >> 16);
			static const auto stub = utils::hook::assemble([](utils::hook::assembler& a)
			{
				a.push(rax);

				a.mov(rax, qword_ptr(rsp, 8));
				a.sub(rax, 2); // Skip the push we inserted

				a.push(rax);
				a.pushad64();

				a.mov(r8, qword_ptr(rsp, 0x80));
				a.mov(rcx, rax);
				a.mov(rdx, rbp);
				a.mov(r9d, 0); // value at rbp+offset -> checksum array offset always start with 0 for basic blocks
				a.call_aligned(adjust_integrity_checksum);

				a.mov(qword_ptr(rsp, 0x78), rax);

				a.popad64();
				a.pop(rax);

				a.add(rsp, 8);

				a.mov(dword_ptr(rdx, rcx, 4), eax);

				a.pop(rax); // return addr
				a.xchg(rax, qword_ptr(rsp)); // switch with push

				a.add(dword_ptr(rbp, rax), 0xFFFFFFFF);

				a.mov(rax, dword_ptr(rdx, rcx, 4)); // restore rax

				a.ret();
			});

			// push other_frame_offset
			utils::hook::set<uint16_t>(game_address, static_cast<uint16_t>(0x6A | (other_frame_offset << 8)));
			utils::hook::call(game_address + 2, stub);
		}

		void patch_big_intact_basic_block_integrity_check(void* address)
		{
			const auto game_address = reinterpret_cast<uint64_t>(address);
			constexpr auto inst_len = 3;

			const auto next_inst_addr = game_address + inst_len;
			const auto* p = reinterpret_cast<const uint8_t*>(next_inst_addr);

			if (!(p[0] == 0x83 && p[1] == 0x85 && p[6] == 0xFF))
			{
				throw std::runtime_error(utils::string::va("Unable to patch intact basic block: %llX", game::derelocate(game_address)));
			}

			const auto other_frame_offset = *reinterpret_cast<uint32_t*>(next_inst_addr + 2);
			static const auto stub = utils::hook::assemble([](utils::hook::assembler& a)
			{
				a.push(rax);

				a.mov(rax, qword_ptr(rsp, 8));
				a.sub(rax, 5); // Skip the push we inserted

				a.push(rax);
				a.pushad64();

				a.mov(r8, qword_ptr(rsp, 0x80));
				a.mov(rcx, rax);
				a.mov(rdx, rbp);
				a.mov(r9, qword_ptr(rsp, 0x90));        // other_frame_offset
				a.mov(r9d, dword_ptr(rbp, r9));         // value at rbp+offset -> checksum array offset
				a.call_aligned(adjust_integrity_checksum);

				a.mov(qword_ptr(rsp, 0x78), rax);

				a.popad64();
				a.pop(rax);

				a.add(rsp, 8);

				a.mov(dword_ptr(rdx, rcx, 4), eax);

				a.pop(rax); // return addr
				a.xchg(rax, qword_ptr(rsp)); // switch with push

				a.add(dword_ptr(rbp, rax), 0xFFFFFFFF);

				a.mov(rax, dword_ptr(rdx, rcx, 4)); // restore rax

				a.ret();
			});

			// push other_frame_offset
			utils::hook::set<uint8_t>(game_address, 0x68);
			utils::hook::set<uint32_t>(game_address + 1, other_frame_offset);

			utils::hook::call(game_address + 5, stub);
		}

		void* find_lea_target(void* start)
		{
			uint8_t* p = static_cast<uint8_t*>(start);

			for (int i = 0; i < 64; ++i)
			{
				uint8_t b = p[i];

				// Check for REX prefix (0x40 - 0x4F)
				if ((b & 0xF0) == 0x40)
				{
					// Next byte must be LEA opcode
					if (p[i + 1] == 0x8D)
					{
						uint8_t modrm = p[i + 2];

						// mod = 00 and r/m = 101 -> RIP-relative
						if ((modrm & 0xC7) == 0x05)
						{
							return utils::hook::extract<void*>(p + i + 3);
						}
					}
				}
			}

			return nullptr;
		}

		void patch_split_basic_block_integrity_check(void* address)
		{
			const auto game_address = reinterpret_cast<uint64_t>(address);
			const auto next_inst_addr = game_address + 3;

			const auto jump_target = find_lea_target(reinterpret_cast<void*>(next_inst_addr));
			if (!jump_target)
			{
				throw std::runtime_error(utils::string::va("Unable to find jump target for split basic block: %llX", game_address));
			}

			const auto stub = utils::hook::assemble([jump_target](utils::hook::assembler& a)
			{
				a.push(rax);

				a.mov(rax, qword_ptr(rsp, 8));
				a.push(rax);

				a.pushad64();

				a.mov(r8, qword_ptr(rsp, 0x80));
				a.mov(rcx, rax);
				a.mov(rdx, rbp);
				a.mov(r9d, 0); // value at rbp+offset -> checksum array offset always start with 0 for basic blocks
				a.call_aligned(adjust_integrity_checksum);

				a.mov(qword_ptr(rsp, 0x78), rax);

				a.popad64();
				a.pop(rax);

				a.add(rsp, 8);

				a.mov(dword_ptr(rdx, rcx, 4), eax);

				a.add(rsp, 8);

				a.jmp(jump_target);
			});

			utils::hook::call(game_address, stub);
		}

		void patch_big_split_basic_block_integrity_check(void* address)
		{
			const auto game_address = reinterpret_cast<uint64_t>(address);
			const auto next_inst_addr = game_address + 3;

			const auto jump_target = find_lea_target(reinterpret_cast<void*>(next_inst_addr));
			if (!jump_target)
			{
				throw std::runtime_error(utils::string::va("Unable to find jump target for split basic block: %llX", game_address));
			}

			const auto other_frame_offset = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint64_t>(jump_target) + 2);
			const auto stub = utils::hook::assemble([jump_target, other_frame_offset](utils::hook::assembler& a)
			{
				a.push(rax);

				a.mov(rax, qword_ptr(rsp, 8));
				a.push(rax);

				a.pushad64();

				a.mov(r8, qword_ptr(rsp, 0x80));
				a.mov(rcx, rax);
				a.mov(rdx, rbp);
				a.mov(r9, other_frame_offset);          // other_frame_offset
				a.mov(r9d, dword_ptr(rbp, r9));         // value at rbp+offset -> checksum array offset
				a.call_aligned(adjust_integrity_checksum);

				a.mov(qword_ptr(rsp, 0x78), rax);

				a.popad64();
				a.pop(rax);

				a.add(rsp, 8);

				a.mov(dword_ptr(rdx, rcx, 4), eax);

				a.add(rsp, 8);

				a.jmp(jump_target);
			});

			utils::hook::call(game_address, stub);
		}

		void patch_integrity_checks_precomputed()
		{
			if (game::environment::is_mp())
			{
				for (const auto offset : mp::intact_integrity_offsets)
				{
					patch_intact_basic_block_integrity_check(reinterpret_cast<void*>(game::relocate(offset)));
				}

				for (const auto offset : mp::big_intact_integrity_offsets)
				{
					patch_big_intact_basic_block_integrity_check(reinterpret_cast<void*>(game::relocate(offset)));
				}

				for (const auto offset : mp::split_integrity_offsets)
				{
					patch_split_basic_block_integrity_check(reinterpret_cast<void*>(game::relocate(offset)));
				}

				for (const auto offset : mp::big_split_integrity_offsets)
				{
					patch_big_split_basic_block_integrity_check(reinterpret_cast<void*>(game::relocate(offset)));
				}
			}
			else
			{
				for (const auto offset : sp::intact_integrity_offsets)
				{
					patch_intact_basic_block_integrity_check(reinterpret_cast<void*>(game::relocate(offset)));
				}

				for (const auto offset : sp::big_intact_integrity_offsets)
				{
					patch_big_intact_basic_block_integrity_check(reinterpret_cast<void*>(game::relocate(offset)));
				}

				for (const auto offset : sp::big_split_integrity_offsets)
				{
					patch_big_split_basic_block_integrity_check(reinterpret_cast<void*>(game::relocate(offset)));
				}
			}	
		}

		void search_and_patch_integrity_checks()
		{
			const auto intact_results = "89 04 8A 83 45 ? FF"_sig;
			for (auto* i : intact_results)
			{
				patch_intact_basic_block_integrity_check(i);
			}

			const auto big_intact_results = "89 04 8A 83 85"_sig;
			for (auto* i : big_intact_results)
			{
				patch_big_intact_basic_block_integrity_check(i);
			}

			/*
			* Needs manually be split up between big and small split integrity checks
			* I think we could handle this in the same function but needs a refactor
			* 
			const auto split_results = "89 04 8A ? ? ? 24 F8"_sig;
			for (auto* i : split_results)
			{
				patch_split_basic_block_integrity_check(i);
			}

			const auto big_split_results = "89 04 8A ? ? ? 24 F8"_sig;
			for (auto* i : big_split_results)
			{
				patch_big_split_basic_block_integrity_check(i);
			}
			*/
		}

		void patch_integrity_checks()
		{
#ifdef PRECOMPUTED
			patch_integrity_checks_precomputed();
#else
			search_and_patch_integrity_checks();
#endif
		}
	}

	struct component final : generic_component
	{
	public:
		void post_thread_setup() override
		{
			patch_integrity_checks();
		}

		component_priority priority() const override
		{
			return component_priority::arxan;
		}
	};
}

REGISTER_COMPONENT(arxan::integrity::component)
