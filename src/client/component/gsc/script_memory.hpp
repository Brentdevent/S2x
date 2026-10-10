#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>

namespace gsc
{
	// Commits custom bytecode pages within an engine-owned reservation.
	class script_memory
	{
	public:
		void initialize(std::uint8_t* base, const std::size_t capacity)
		{
			if (!clear())
			{
				throw std::runtime_error("Could not decommit custom script memory");
			}

			base_ = base;
			capacity_ = capacity;
		}

		std::uint8_t* allocate(const std::size_t size, const std::size_t alignment)
		{
			if (!base_ || !size || !alignment || (alignment & (alignment - 1)) || alignment > page_size)
			{
				throw std::runtime_error("Invalid custom script memory allocation");
			}

			const auto offset = (used_ + alignment - 1) & ~(alignment - 1);
			if (offset > capacity_ || size > capacity_ - offset)
			{
				throw std::runtime_error(std::format("Out of custom script memory while allocating {} bytes", size));
			}

			const auto end = offset + size;
			const auto committed = (end + page_size - 1) & ~(page_size - 1);
			if (committed > committed_)
			{
				if (!VirtualAlloc(base_ + committed_, committed - committed_, MEM_COMMIT, PAGE_READWRITE))
				{
					throw std::runtime_error(std::format("Could not commit custom script memory (Windows error {})", GetLastError()));
				}
			}

			committed_ = committed;
			used_ = end;
			return base_ + offset;
		}

		bool clear()
		{
			if (committed_ && !VirtualFree(base_, committed_, MEM_DECOMMIT))
			{
				return false;
			}

			used_ = 0;
			committed_ = 0;
			return true;
		}

	private:
		// Matches the engine's 4 KiB page alignment.
		static constexpr std::size_t page_size = 0x1000;
		std::uint8_t* base_ = nullptr;
		std::size_t capacity_ = 0;
		std::size_t used_ = 0;
		std::size_t committed_ = 0;
	};
}
