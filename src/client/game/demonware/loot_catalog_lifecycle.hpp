#pragma once

#include "loot_catalog.hpp"
#include "loot_compatibility.hpp"

#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace demonware::runtime
{
	// Only poll() touches the main-producer cursor. Invalidation/load/shutdown
	// may originate elsewhere and never invoke the provider or game functions.
	class loot_catalog_lifecycle
	{
	public:
		std::shared_ptr<const loot_catalog::catalog> snapshot() const
		{
			return current_.load(std::memory_order_acquire);
		}

		void invalidate()
		{
			std::lock_guard lock{publication_mutex_};
			invalidate_locked();
		}

		void begin_load()
		{
			std::lock_guard lock{publication_mutex_};
			load_depth_.fetch_add(1, std::memory_order_release);
			invalidate_locked();
		}

		void end_load() { load_depth_.fetch_sub(1, std::memory_order_release); }

		void stop()
		{
			std::lock_guard lock{publication_mutex_};
			stopped_.store(true, std::memory_order_release);
			invalidate_locked();
		}

		template <typename Readiness, typename Provider>
		bool poll(Readiness&& database_ready, Provider&& provider)
		{
			if (stopped_.load(std::memory_order_acquire))
			{
				return false;
			}
			if (load_depth_.load(std::memory_order_acquire) || !std::invoke(database_ready))
			{
				if (was_ready_)
				{
					invalidate();
				}
				was_ready_ = false;
				return false;
			}
			was_ready_ = true;
			const auto revision = requested_.load(std::memory_order_acquire);
			if (attempted_ == revision)
			{
				return false;
			}
			// Load entry hooks take this lock BEFORE entering the stock DB writer
			// path, then release it with load_depth_ set. Never acquire it for the
			// first time from inside the engine's asset-writer critical section.
			std::lock_guard lock{publication_mutex_};
			if (stopped_.load(std::memory_order_relaxed))
			{
				return false;
			}
			// A load may have begun and returned after the cheap outer check.
			// Re-read its asynchronous completion state while load entry is
			// excluded, before admitting any live DB reads or consuming a revision.
			if (load_depth_.load(std::memory_order_relaxed) || !std::invoke(database_ready))
			{
				if (was_ready_)
				{
					invalidate_locked();
				}
				was_ready_ = false;
				return false;
			}
			if (requested_.load(std::memory_order_relaxed) != revision)
			{
				return false;
			}
			// Failed/invalid generations stay unavailable until a real transition
			// or explicit invalidation, rather than retrying a full scan each frame.
			attempted_ = revision;
			try
			{
				auto result = std::forward<Provider>(provider)(revision);
				if (stopped_.load(std::memory_order_relaxed) || load_depth_.load(std::memory_order_relaxed) ||
					requested_.load(std::memory_order_relaxed) != revision || !result || !valid(*result))
				{
					return false;
				}
				result->generation = revision;

				current_.store(std::make_shared<const loot_catalog::catalog>(std::move(*result)),
					std::memory_order_release);
				return true;
			}
			catch (const std::exception&) { return false; }
		}

	private:
		static bool valid(const loot_catalog::catalog& value)
		{
			const auto& source = value.source_table;
			if (!source.table_asset_available || !source.table_values_available ||
				!source.table_dimensions_valid || !source.required_columns_available ||
				source.table_row_count <= 0 || source.table_row_count > 100000 ||
				source.table_column_count <= 56 || source.table_column_count > 256 ||
				value.items.size() != static_cast<std::size_t>(source.table_row_count))
			{
				return false;
			}
			for (const auto id : {"sd_mp", "sd_mp_rare"})
			{
				const auto drop = loot_catalog::find_supply_drop(value, id);
				if (!drop || !loot_compatibility::is_confirmed_mp_supply_drop(*drop))
				{
					return false;
				}
			}
			return true;
		}

		void invalidate_locked()
		{
			requested_.fetch_add(1, std::memory_order_release);
			current_.store({}, std::memory_order_release);
		}
		std::atomic<std::shared_ptr<const loot_catalog::catalog>> current_{};
		std::recursive_mutex publication_mutex_{};
		std::atomic_uint64_t requested_{1};
		std::atomic_uint32_t load_depth_{};
		std::atomic_bool stopped_{};
		std::uint64_t attempted_{};
		bool was_ready_{};
	};

	loot_catalog_lifecycle& loot_lifecycle();
}
