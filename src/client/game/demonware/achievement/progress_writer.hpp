#pragma once

#include "order_progress.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace demonware::order_progress
{
	// Owns copied observations, never native queues, clocks, assets or UI objects.
	class progress_writer
	{
	public:
		struct request
		{
			occurrence value;
			std::uint64_t stream{};
			bool refresh{};
		};

		struct notification
		{
			std::uint64_t stream{};
			std::vector<achievement_record> records;
		};

		struct completion
		{
			// Terminal changes need the full fetch; ordinary increments refresh only these names.
			bool full_refresh{};
			std::vector<std::string> progress_names;
			std::vector<notification> notifications;
		};

		~progress_writer();
		void start();
		void enqueue(request value);
		completion take_completed();
		std::vector<usage> pending_usage();
		std::size_t pending_count();
		bool failed();

		// Stops admission, joins the worker and retries outstanding saves before returning.
		bool finish();

	private:
		void run();

		std::mutex mutex_;
		std::condition_variable wake_;
		std::deque<request> pending_;
		completion completed_;
		std::thread thread_;
		bool stopping_{};
		bool failed_{};
	};
}
