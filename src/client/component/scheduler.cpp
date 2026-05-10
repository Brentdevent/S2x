#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "scheduler.hpp"

#include "game/game.hpp"

#include <cassert>
#include <utils/hook.hpp>
#include <utils/concurrency.hpp>
#include <utils/thread.hpp>

namespace scheduler
{
	namespace
	{
		struct task
		{
			std::function<bool()> handler{};
			std::chrono::milliseconds interval{};
			std::chrono::high_resolution_clock::time_point last_call{};
		};

		using task_list = std::vector<task>;

		class task_pipeline
		{
		public:
			void add(task&& task)
			{
				new_callbacks_.access([&task](task_list& tasks)
				{
					tasks.emplace_back(std::move(task));
				});
			}

			void execute()
			{
				callbacks_.access([&](task_list& tasks)
				{
					this->merge_callbacks();

					for (auto i = tasks.begin(); i != tasks.end();)
					{
						const auto now = std::chrono::high_resolution_clock::now();
						const auto diff = now - i->last_call;

						if (diff < i->interval)
						{
							++i;
							continue;
						}

						i->last_call = now;

						const auto res = i->handler();
						if (res == cond_end)
						{
							i = tasks.erase(i);
						}
						else
						{
							++i;
						}
					}
				});
			}

		private:
			utils::concurrency::container<task_list> new_callbacks_;
			utils::concurrency::container<task_list, std::recursive_mutex> callbacks_;

			void merge_callbacks()
			{
				callbacks_.access([&](task_list& tasks)
				{
					new_callbacks_.access([&](task_list& new_tasks)
					{
						tasks.insert(tasks.end(), std::move_iterator<task_list::iterator>(new_tasks.begin()),
						             std::move_iterator<task_list::iterator>(new_tasks.end()));
						new_tasks = {};
					});
				});
			}
		};

		volatile bool kill = false;
		std::thread thread;
		task_pipeline pipelines[pipeline::count];

		utils::hook::detour r_end_frame_hook;
		utils::hook::detour g_run_frame_hook;
		utils::hook::detour main_frame_hook;

		void execute(const pipeline type)
		{
			assert(type >= 0 && type < pipeline::count);
			pipelines[type].execute();
		}

		void r_end_frame_stub()
		{
			execute(pipeline::renderer);
			r_end_frame_hook.invoke<void>();
		}

		void g_clear_vehicle_inputs_stub()
		{
			// game::G_ClearVehicleInputs();
			execute(pipeline::server);
		}

		void server_frame_stub()
		{
			g_run_frame_hook.invoke<void>();
			execute(pipeline::server);
		}

		void main_frame_stub()
		{
			main_frame_hook.invoke<void>();
			execute(pipeline::main);
		}
	}

	void schedule(const std::function<bool()>& callback, const pipeline type,
	              const std::chrono::milliseconds delay)
	{
		assert(type >= 0 && type < pipeline::count);

		task task;
		task.handler = callback;
		task.interval = delay;
		task.last_call = std::chrono::high_resolution_clock::now();

		pipelines[type].add(std::move(task));
	}

	void loop(const std::function<void()>& callback, const pipeline type,
	          const std::chrono::milliseconds delay)
	{
		schedule([callback]()
		{
			callback();
			return cond_continue;
		}, type, delay);
	}

	void once(const std::function<void()>& callback, const pipeline type,
	          const std::chrono::milliseconds delay)
	{
		schedule([callback]()
		{
			callback();
			return cond_end;
		}, type, delay);
	}

	class component final : public generic_component
	{
	public:
		void post_load() override
		{
			thread = utils::thread::create_named_thread("Async Scheduler", []()
			{
				while (!kill)
				{
					execute(pipeline::async);
					std::this_thread::sleep_for(10ms);
				}
			});
		}

		void post_unpack() override
		{
			// not sure about this one
			r_end_frame_hook.create(game::select(0x8BBCD0, 0x63A970), r_end_frame_stub);

			// Some random function in Com_Frame I suppose
			main_frame_hook.create(game::select(0x78AAE0, 0x46FEC0), main_frame_stub);

			//utils::hook::call(0x1421F8DBE_g, g_clear_vehicle_inputs_stub);
		}

		void pre_destroy() override
		{
			kill = true;
			if (thread.joinable())
			{
				thread.join();
			}
		}

		component_priority priority() const override
		{
			return component_priority::updater;
		}
	};
}

REGISTER_COMPONENT(scheduler::component)
