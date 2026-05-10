#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "integrity_locations.hpp"
#include "breakpoint_locations.hpp"
#include "code_healing_locations.hpp"

#include "game/game.hpp"

#include "utils/hook.hpp"
#include "utils/string.hpp"

#include <span>

#define PRECOMPUTED

namespace arxan::code_healing
{
	namespace
	{
		using patch_list = std::span<const uint64_t>;

		struct arxan_patch_lists
		{
			patch_list intact_integrity;
			patch_list big_intact_integrity;
			patch_list split_integrity;
			patch_list big_split_integrity;
			patch_list al_healing;
			patch_list al_increment_healing;
			patch_list eax_healing;
			patch_list eax_split_healing;
			patch_list int2d_breakpoint;
		};

		constexpr std::array<uint64_t, 0> empty_offsets{};

		constexpr arxan_patch_lists mp_patches
		{
			mp::intact_integrity_offsets,
			mp::big_intact_integrity_offsets,
			mp::split_integrity_offsets,
			mp::big_split_integrity_offsets,
			mp::al_healing_offsets,
			mp::al_increment_healing_offsets,
			mp::eax_healing_offsets,
			mp::eax_split_healing_offsets,
			mp::int2d_breakpoint_offsets,
		};

		constexpr arxan_patch_lists sp_patches
		{
			sp::intact_integrity_offsets,
			sp::big_intact_integrity_offsets,
			empty_offsets,
			sp::big_split_integrity_offsets,
			sp::al_healing_offsets,
			sp::al_increment_healing_offsets,
			sp::eax_healing_offsets,
			sp::eax_split_healing_offsets,
			sp::int2d_breakpoint_offsets,
		};

		const arxan_patch_lists& current_patches()
		{
			return game::environment::is_mp()
				? mp_patches
				: sp_patches;
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

		struct patch_region
		{
			patch_list offsets;
			uint64_t size;
		};

		bool overlaps_region(const uint64_t heal_start, const uint64_t heal_end, const patch_region& region)
		{
			for (const auto patched_address : region.offsets)
			{
				const auto patch_start = game::relocate(patched_address);
				const auto patch_end = patch_start + region.size;

				// [heal_start, heal_end) overlaps [patch_start, patch_end)
				if (heal_start < patch_end && heal_end > patch_start)
				{
					return true;
				}
			}

			return false;
		}

		bool allow_code_healing(void* heal_location, const uint32_t patch_length)
		{
			const auto heal_start = reinterpret_cast<uint64_t>(heal_location);
			const auto heal_end = heal_start + patch_length;

			const auto& patches = current_patches();

			const std::array regions
			{
				patch_region{patches.big_intact_integrity, 0xA},
				patch_region{patches.big_split_integrity, 0x5},
				patch_region{patches.intact_integrity, 0x7},
				patch_region{patches.split_integrity, 0x5},
				patch_region{patches.al_healing, 0x6},
				patch_region{patches.al_increment_healing, 0x6},
				patch_region{patches.eax_healing, 0x5},
				patch_region{patches.eax_split_healing, 0x5},
				patch_region{patches.int2d_breakpoint, 0x7},
			};

			for (const auto& region : regions)
			{
				if (overlaps_region(heal_start, heal_end, region))
				{
#ifdef ARXAN_DEBUG
					OutputDebugStringA(utils::string::va("Blocked code healing at: %llX for size: %X\n", game::derelocate(heal_start), patch_length));
#endif
					return false;
				}
			}

			return true;
		}

		void patch_healing_code_sections_function_al(void* address)
		{
			const auto game_address = reinterpret_cast<uint64_t>(address);

			static const auto stub = utils::hook::assemble([](utils::hook::assembler& a)
			{
				a.pushad64();

				const auto skip_update = a.new_label();

				a.mov(rcx, rdx); // rdx = destination pointer
				a.mov(edx, 1); // length = 1 byte (size of al)
				a.call_aligned(allow_code_healing);

				a.test(al, al);
				a.jz(skip_update);

				a.popad64();

				// Original instructions we overwrite:
				// mov [rdx], al
				// add dword ptr [rbp+20h], 0FFFFFFFFh
				a.mov(byte_ptr(rdx), al);
				a.add(dword_ptr(rbp, 0x20), -1);
				a.ret();

				a.bind(skip_update);
				a.popad64();

				// Skip overwrite but decrease counter
				a.add(dword_ptr(rbp, 0x20), -1);
				a.ret();
			});

			// Call overwrites 5 bytes, we nop 1 extra byte and mimic the needed instructions in our asm stub.
			utils::hook::nop(game_address, 6);
			utils::hook::call(game_address, stub);
		}

		void patch_healing_code_sections_function_al_increment(void* address)
		{
			const auto game_address = reinterpret_cast<uint64_t>(address);

			static const auto stub = utils::hook::assemble([](utils::hook::assembler& a)
			{
				a.pushad64();

				const auto skip_update = a.new_label();

				a.mov(rcx, rdx); // rdx = destination pointer
				a.mov(edx, 1); // length = 1 byte (size of al)
				a.call_aligned(allow_code_healing);

				a.test(al, al);
				a.jz(skip_update);

				a.popad64();

				// Original instructions we overwrite:
				// mov [rdx], al
				// add dword ptr [rbp+10h], 1
				a.mov(byte_ptr(rdx), al);
				a.add(dword_ptr(rbp, 0x10), 1);
				a.ret();

				a.bind(skip_update);
				a.popad64();

				// Skip overwrite but increase counter
				a.add(dword_ptr(rbp, 0x10), 1);
				a.ret();
			});

			// total 6 bytes. call = 5 bytes, so nop 1 extra.
			utils::hook::nop(game_address, 6);
			utils::hook::call(game_address, stub);
		}

		void patch_healing_code_sections_function_eax(void* address)
		{
			const auto game_address = reinterpret_cast<uint64_t>(address);

			static const auto stub = utils::hook::assemble([](utils::hook::assembler& a)
			{
				a.pushad64();

				const auto skip_update = a.new_label();

				a.mov(rcx, rdx); // rdx = destination pointer
				a.mov(edx, 4); // length = 4 bytes (size of eax)
				a.call_aligned(allow_code_healing);

				a.test(al, al);
				a.jz(skip_update);

				a.popad64();

				// Original instructions we overwrite:
				// mov [rdx], eax
				// mov eax, [rbp+20h]
				a.mov(dword_ptr(rdx), eax);
				a.mov(eax, dword_ptr(rbp, 0x20));
				a.ret();

				a.bind(skip_update);
				a.popad64();

				// Skip overwrite but reload original eax value
				a.mov(eax, dword_ptr(rbp, 0x20));
				a.ret();
			});

			utils::hook::call(game_address, stub);
		}

		void patch_healing_code_sections_function_eax_split(void* address)
		{
			const auto game_address = reinterpret_cast<uint64_t>(address);
			const auto jump_target = find_lea_target(address);

			if (!jump_target)
			{
				throw std::runtime_error("Failed to find jump target for split eax healing function");
			}

			const auto stub = utils::hook::assemble([jump_target](utils::hook::assembler& a)
			{
				// Original instructions we overwrite/skip:
				// mov rdx, [rbp+10h]
				// mov eax, [rax]
				a.mov(rdx, qword_ptr(rbp, 0x10));
				a.mov(eax, dword_ptr(rax));

				// Our code
				a.pushad64();

				const auto skip_update = a.new_label();

				a.mov(rcx, rdx); // rdx = destination pointer
				a.mov(edx, 4); // length = 4 bytes (size of eax)
				a.call_aligned(allow_code_healing);

				a.test(al, al);
				a.jz(skip_update);

				a.popad64();

				// Overwrite destination with eax value like original code
				a.mov(dword_ptr(rdx), eax);
				a.add(rsp, 8); // Remove return address from stack
				a.jmp(jump_target);

				a.bind(skip_update);
				a.popad64();
				a.add(rsp, 8); // Remove return address from stack
				a.jmp(jump_target);
			});

			utils::hook::call(game_address, stub);
		}

		void patch_code_healing_precomputed()
		{
			const auto& patches = current_patches();

			// This needs to be patched first as it could heal other heal patches
			for (const auto offset : patches.al_increment_healing)
			{
				patch_healing_code_sections_function_al_increment(reinterpret_cast<void*>(game::relocate(offset)));
			}

			for (const auto offset : patches.al_healing)
			{
				patch_healing_code_sections_function_al(reinterpret_cast<void*>(game::relocate(offset)));
			}

			for (const auto offset : patches.eax_healing)
			{
				patch_healing_code_sections_function_eax(reinterpret_cast<void*>(game::relocate(offset)));
			}

			for (const auto offset : patches.eax_split_healing)
			{
				patch_healing_code_sections_function_eax_split(reinterpret_cast<void*>(game::relocate(offset)));
			}
		}

		void search_and_patch_code_healing()
		{
			// This needs to be patched first as it could heal other heal patches
			const auto al_increment_healing_results = "88 02 83 45 10 01"_sig;
			for (auto* i : al_increment_healing_results)
			{
				patch_healing_code_sections_function_al_increment(i);
			}

			const auto al_healing_results = "88 02 83 45 20 FF"_sig;
			for (auto* i : al_healing_results)
			{
				patch_healing_code_sections_function_al(i);
			}

			const auto eax_healing_results = "89 02 8B 45 20"_sig;
			for (auto* i : eax_healing_results)
			{
				patch_healing_code_sections_function_eax(i);
			}

			const auto eax_split_healing_results = "48 8B 55 10 8B 00 89 02 ? ? ? 24 F8"_sig;
			for (auto* i : eax_split_healing_results)
			{
				patch_healing_code_sections_function_eax_split(i);
			}
		}

		void patch_code_healing()
		{
#ifdef PRECOMPUTED
			patch_code_healing_precomputed();
#else
			search_and_patch_code_healing();
#endif
		}
	}

	struct component final : generic_component
	{
	public:
		void post_thread_setup() override
		{
			patch_code_healing();
		}

		component_priority priority() const override
		{
			return component_priority::arxan;
		}
	};
}

REGISTER_COMPONENT(arxan::code_healing::component)
