#include <std_include.hpp>
#include "progress_writer.hpp"

namespace demonware::order_progress
{
	namespace
	{
		using namespace std::chrono_literals;

		constexpr std::size_t batch_limit = 256;
		constexpr auto batch_delay = 50ms;
		constexpr auto retry_delay = 1s;
		constexpr auto shutdown_retry_delay = 100ms;
		constexpr unsigned shutdown_attempt_limit = 3;
	}

	progress_writer::~progress_writer()
	{
		finish();
	}

	void progress_writer::start()
	{
		thread_ = std::thread{&progress_writer::run, this};
	}

	void progress_writer::enqueue(request value)
	{
		std::lock_guard lock{mutex_};
		if (stopping_)
		{
			throw std::runtime_error("Order progress writer has stopped");
		}

		pending_.push_back(std::move(value));
		wake_.notify_one();
	}

	progress_writer::completion progress_writer::take_completed()
	{
		std::lock_guard lock{mutex_};
		return std::exchange(completed_, {});
	}

	std::vector<usage> progress_writer::pending_usage()
	{
		std::lock_guard lock{mutex_};
		std::vector<usage> result;
		for (const auto& request : pending_)
		{
			for (const auto& value : request.value.elapsed)
			{
				const auto found = std::ranges::find_if(result, [&](const auto& previous)
				{
					return previous.achievement.name == value.achievement.name &&
						previous.achievement.activation == value.achievement.activation;
				});

				if (found == result.end())
				{
					result.push_back(value);
				}
				else
				{
					found->remaining = std::min(found->remaining, value.remaining);
				}
			}
		}

		return result;
	}

	std::size_t progress_writer::pending_count()
	{
		std::lock_guard lock{mutex_};
		return pending_.size();
	}

	bool progress_writer::failed()
	{
		std::lock_guard lock{mutex_};
		return failed_;
	}

	bool progress_writer::finish()
	{
		{
			std::lock_guard lock{mutex_};
			stopping_ = true;
		}

		wake_.notify_one();
		if (thread_.joinable())
		{
			thread_.join();
		}

		return pending_count() == 0;
	}

	void progress_writer::run()
	{
		unsigned shutdown_failures{};
		std::unique_lock lock{mutex_};

		for (;;)
		{
			wake_.wait(lock, [&] { return stopping_ || !pending_.empty(); });
			if (pending_.empty())
			{
				return;
			}

			// Batch a short burst into one durable profile replacement. Stop skips this delay.
			wake_.wait_for(lock, batch_delay, [&] { return stopping_; });
			const auto count = std::min(pending_.size(), batch_limit);
			std::vector<occurrence> batch;
			batch.reserve(count);

			for (std::size_t i = 0; i < count; ++i)
			{
				batch.push_back(pending_[i].value);
			}

			// Do not hold the queue lock while acquiring store locks or doing disk IO.
			lock.unlock();
			std::vector<std::vector<achievement_record>> progressed;
			bool terminal_changed{};
			auto result = achievement_store::mutation_result::save_failed;
			try
			{
				result = settle_many(batch, progressed, terminal_changed);
			}
			catch (const std::exception&)
			{
				// Store mutations roll back on failure; retain the whole prefix for retry.
			}

			lock.lock();

			failed_ = result == achievement_store::mutation_result::save_failed;
			if (failed_)
			{
				if (stopping_ && ++shutdown_failures >= shutdown_attempt_limit)
				{
					return;
				}

				if (stopping_)
				{
					wake_.wait_for(lock, shutdown_retry_delay);
				}
				else
				{
					wake_.wait_for(lock, retry_delay, [&] { return stopping_; });
				}

				continue;
			}

			shutdown_failures = 0;

			if (result == achievement_store::mutation_result::updated)
			{
				completed_.full_refresh |= terminal_changed;
				for (std::size_t i = 0; i < progressed.size(); ++i)
				{
					const auto& request = pending_[i];
					if (request.refresh)
					{
						// An empty result can be a timer stop or expiry, without a counter increment.
						completed_.full_refresh |= progressed[i].empty();
						for (const auto& record : progressed[i])
						{
							if (std::ranges::find(completed_.progress_names, record.name) == completed_.progress_names.end())
							{
								completed_.progress_names.push_back(record.name);
							}
						}
					}

					if (request.stream && !progressed[i].empty())
					{
						completed_.notifications.push_back({request.stream, std::move(progressed[i])});
					}
				}
			}

			// Retire only committed/unchanged work. Later arrivals stay behind this prefix.
			pending_.erase(pending_.begin(), pending_.begin() + count);
		}
	}
}
