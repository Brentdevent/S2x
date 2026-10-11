#pragma once
#include <array>

#include <utils/string.hpp>

#include "game/types/demonware.hpp"

#include "component/console/console.hpp"

#include "servers/service_server.hpp"

namespace demonware
{
	class service
	{
		using callback_t = std::function<void(service_server*, byte_buffer*)>;

		uint8_t id_;
		std::string name_;
		std::mutex mutex_;
		uint8_t task_id_;
		std::map<uint8_t, callback_t> tasks_;
		std::array<std::uint32_t, 256> task_execution_log_counts_{};
		std::array<std::uint32_t, 256> missing_task_log_counts_{};

	public:
		virtual ~service() = default;
		service(service&&) = delete;
		service(const service&) = delete;
		service& operator=(const service&) = delete;

		service(const uint8_t id, std::string name) : id_(id), name_(std::move(name)), task_id_(0)
		{
		}

		uint8_t id() const
		{
			return this->id_;
		}

		const std::string& name() const
		{
			return this->name_;
		}

		uint8_t task_id() const
		{
			return this->task_id_;
		}

		virtual void exec_task(service_server* server, const std::string& data)
		{
			std::lock_guard<std::mutex> _(this->mutex_);

			byte_buffer buffer(data);
			uint8_t task_id{};
			if (!buffer.read_ubyte(&task_id))
			{
				console::error("[DW] %s: malformed task header\n", name_.data());
				server->create_reply(0, game::demonware::BD_PARAM_PARSE_ERROR).send();
				return;
			}

			this->task_id_ = task_id;

			const auto& it = this->tasks_.find(this->task_id_);

			if (it != this->tasks_.end())
			{
				constexpr std::uint32_t maximum_task_execution_logs = 32;
				auto& log_count = this->task_execution_log_counts_[this->task_id_];
				if (log_count < maximum_task_execution_logs)
				{
					console::demonware(
						"[DW] %s: executing task '%d' (transaction ID: %llu)\n",
						name_.data(), this->task_id_, service_reply::transaction_id + 1);
				}
				else if (log_count == maximum_task_execution_logs)
				{
					console::demonware(
						"[DW] %s: further task '%d' execution logs suppressed\n",
						name_.data(), this->task_id_);
				}
				if (log_count <= maximum_task_execution_logs)
				{
					++log_count;
				}

				it->second(server, &buffer);
			}
			else
			{
				constexpr std::uint32_t maximum_missing_task_logs = 8;
				auto& log_count = this->missing_task_log_counts_[this->task_id_];
				if (log_count < maximum_missing_task_logs)
				{
					console::error("[DW] %s: missing task '%d'\n", name_.data(),
						this->task_id_);
				}
				else if (log_count == maximum_missing_task_logs)
				{
					console::error(
						"[DW] %s: further missing-task '%d' logs suppressed\n",
						name_.data(), this->task_id_);
				}
				if (log_count <= maximum_missing_task_logs)
				{
					++log_count;
				}

				// return no error
				server->create_reply(this->task_id_).send();
			}
		}

	protected:

		template <typename Class, typename T, typename... Args>
		void register_task(const uint8_t id, T (Class::* callback)(Args ...) const)
		{
			this->tasks_[id] = [this, callback](Args ... args) -> T
			{
				return (reinterpret_cast<Class*>(this)->*callback)(args...);
			};
		}

		template <typename Class, typename T, typename... Args>
		void register_task(const uint8_t id, T (Class::* callback)(Args ...))
		{
			this->tasks_[id] = [this, callback](Args ... args) -> T
			{
				return (reinterpret_cast<Class*>(this)->*callback)(args...);
			};
		}
	};
}
