#include <std_include.hpp>

#include "collision.hpp"
#include "game/types/collision.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <cmath>

namespace material_picker::collision
{
	namespace
	{
		// Same contents mask as the native crosshair trace at 0xF8A5F.
		constexpr std::uint64_t trace_mask = 41965601;
		struct capture
		{
			const game::Trace* trace;
			const game::CollisionMaterial* material{};
		};

		thread_local capture* active_capture{};
		std::atomic_uint32_t captures{};
		static_assert(std::atomic_uint32_t::is_always_lock_free && sizeof(captures) == 4);

		void capture_material(const game::Trace* trace, const game::CollisionMaterial* material) noexcept
		{
			if (active_capture && active_capture->trace == trace)
			{
				active_capture->material = material;
			}
		}

		void material_hit_stub(utils::hook::assembler& a, const bool brush)
		{
			// Ordinary gameplay traces bypass the observer and its register saves.
			const auto native = a.new_label();
			a.push(rax);
			a.mov(rax, reinterpret_cast<std::uintptr_t>(&captures));
			a.cmp(dword_ptr(rax), 0);
			a.pop(rax);
			a.je(native);

			// Both sites load surface flags from the winning collision material.
			// Preserve all GPRs and volatile XMMs across the observer, then replay
			// both displaced MOVs exactly. Flags are dead here: the brush immediately
			// TESTs SI; the terrain path only restores registers and returns.
			a.pushad64();
			a.sub(rsp, 0x60);
			for (unsigned int i = 0; i < 6; ++i)
			{
				a.movdqu(ptr(rsp, i * 16), xmm(i));
			}

			if (brush)
			{
				a.lea(rdx, ptr(rax, rcx, 3));
				a.mov(rcx, rbx);
			}
			else
			{
				a.lea(rdx, ptr(rcx, rax, 3));
				a.mov(rcx, rsi);
			}

			a.call_aligned(capture_material);
			for (unsigned int i = 0; i < 6; ++i)
			{
				a.movdqu(xmm(i), ptr(rsp, i * 16));
			}

			a.add(rsp, 0x60);
			a.popad64();
			a.bind(native);
			if (brush)
			{
				a.mov(ecx, dword_ptr(rax, rcx, 3, 8));
				a.mov(dword_ptr(rbx, 0x10), ecx);
				a.jmp(game::select(0x645C61, 0x460811));
			}
			else
			{
				a.mov(eax, dword_ptr(rcx, rax, 3, 8));
				a.mov(dword_ptr(rsi, 0x10), eax);
				a.jmp(game::select(0x640CE6, 0x45C366));
			}
		}
	}

	std::optional<picked_material> pick(const geometry::ray& ray)
	{
		const auto* world = reinterpret_cast<const game::clipMap_t*>(game::select(0xAA73E90, 0x97D3910));
		if (!world->material_count || !world->materials)
		{
			return {};
		}

		const auto* origin = ray.origin.data();
		float end[3];
		for (unsigned int i = 0; i < 3; ++i)
		{
			end[i] = origin[i] + ray.direction[i] * trace_distance;
		}

		game::Trace trace{};
		capture result{&trace};
		{
			struct scoped_capture
			{
				capture* previous{active_capture};
				explicit scoped_capture(capture& value)
				{
					active_capture = &value;
					++captures;
				}

				~scoped_capture()
				{
					active_capture = previous;
					--captures;
				}
			} scope(result);

			// CG's world-only box trace wrapper supplies the native client glass
			// visibility callback, then calls CM_BoxTrace.
			// Bounds is six floats (midpoint, half-size); all zero makes a ray.
			const float bounds[6]{};
			utils::hook::invoke<void>(game::select(0x459840, 0x25B470), &trace, origin, end, bounds, trace_mask, nullptr);
		}

		if (!std::isfinite(trace.fraction) || trace.fraction < 0.0f || trace.fraction >= 1.0f || trace.start_solid
			|| trace.all_solid || !result.material || !result.material->name)
		{
			return std::nullopt;
		}

		// Retain only copied values; the collision material may unload between frames.
		picked_material picked;
		picked.name = result.material->name;
		picked.distance = trace.fraction * trace_distance;
		picked.surface_flags = trace.surface_flags;
		picked.contents = trace.contents;

		return picked;
	}

	void initialize()
	{
		// Two seven-byte MOV pairs, verified in both supported binaries.
		utils::hook::jump(game::select(0x645C5A, 0x46080A),
			utils::hook::assemble(
				[](auto& a)
				{
					material_hit_stub(a, true);
				}));
		utils::hook::nop(game::select(0x645C5F, 0x46080F), 2);
		utils::hook::jump(game::select(0x640CDF, 0x45C35F),
			utils::hook::assemble(
				[](auto& a)
				{
					material_hit_stub(a, false);
				}));
		utils::hook::nop(game::select(0x640CE4, 0x45C364), 2);
	}
}
