#include <std_include.hpp>

#include "callstack.hpp"

#include "game/game.hpp"

#include <utils/nt.hpp>
#include <utils/string.hpp>

namespace callstack
{
	namespace
	{
		constexpr size_t max_frames = 64;
		constexpr size_t game_image_base = 0x140000000;

		size_t walk(CONTEXT context, DWORD64* frames, const size_t max)
		{
			size_t count = 0;

			__try
			{
				while (count < max && context.Rip)
				{
					frames[count++] = context.Rip;

					const auto previous_rsp = context.Rsp;
					DWORD64 image_base{};
					auto* entry = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
					if (entry)
					{
						void* handler_data{};
						DWORD64 establisher_frame{};
						RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, entry, &context, &handler_data,
							&establisher_frame, nullptr);
					}
					else
					{
						context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
						context.Rsp += 8;
					}

					if (context.Rsp <= previous_rsp)
					{
						break;
					}
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
			}

			return count;
		}

		std::string format_frame(const DWORD64 address)
		{
			const auto* pointer = reinterpret_cast<const void*>(address);
			const auto module = utils::nt::library::get_by_address(pointer);
			if (!module)
			{
				return utils::string::va("0x%llX", address);
			}

			const auto rva = address - reinterpret_cast<DWORD64>(module.get_ptr());
			if (reinterpret_cast<size_t>(module.get_ptr()) == game::get_base())
			{
				return utils::string::va("%s+0x%llX (0x%llX)", module.get_name().data(), rva, game_image_base + rva);
			}

			return utils::string::va("%s+0x%llX", module.get_name().data(), rva);
		}
	}

	std::string format(const CONTEXT& context)
	{
		DWORD64 frames[max_frames]{};
		const auto count = walk(context, frames, max_frames);

		std::string result{};
		for (size_t i = 0; i < count; ++i)
		{
			result.append(utils::string::va("\t%02zu: %s\r\n", i, format_frame(frames[i]).data()));
		}

		return result;
	}

	std::string capture()
	{
		CONTEXT context{};
		RtlCaptureContext(&context);
		return format(context);
	}
}
