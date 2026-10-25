#pragma once

#include "geometry.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>
#include <utility>

namespace material_picker
{
	// Worker callbacks own their payloads. Advancing or disabling invalidates
	// outstanding tickets, including callbacks that finish after a map change.
	template <typename Payload> class frame_capture
	{
	public:
		struct ticket
		{
			std::uint64_t epoch;
			geometry::ray ray;
		};

		struct snapshot
		{
			std::vector<Payload> meshes;
			bool truncated{};
		};

		std::optional<ticket> acquire()
		{
			if (!enabled_.load(std::memory_order_relaxed))
			{
				return {};
			}

			std::lock_guard lock(mutex_);

			return ray_ ? std::optional<ticket>{{epoch_, *ray_}} : std::nullopt;
		}

		void submit(const ticket& request, const std::uint32_t frame, Payload payload, const size_t triangles)
		{
			std::lock_guard lock(mutex_);
			if (!ray_ || request.epoch != epoch_)
			{
				return;
			}

			if (triangles > max_triangles - triangle_count_)
			{
				truncated_ = true;

				return;
			}

			triangle_count_ += triangles;
			entries_.push_back({frame, std::move(payload)});
		}

		snapshot advance(const std::uint32_t frame, const std::optional<geometry::ray>& next)
		{
			std::lock_guard lock(mutex_);
			snapshot result;
			if (ray_)
			{
				for (auto& entry : entries_)
				{
					if (entry.frame == frame)
					{
						result.meshes.push_back(std::move(entry.payload));
					}
				}

				result.truncated = truncated_;
			}

			entries_.clear();
			triangle_count_ = 0;
			truncated_ = false;
			++epoch_;
			ray_ = next;
			enabled_.store(next.has_value(), std::memory_order_relaxed);

			return result;
		}

		static constexpr size_t max_triangles = 131072;

	private:
		struct entry
		{
			std::uint32_t frame;
			Payload payload;
		};

		std::mutex mutex_;
		std::atomic_bool enabled_{};
		std::uint64_t epoch_{};
		std::optional<geometry::ray> ray_;
		std::vector<entry> entries_;
		size_t triangle_count_{};
		bool truncated_{};
	};
}
