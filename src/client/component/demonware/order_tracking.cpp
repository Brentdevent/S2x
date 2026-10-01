#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "component/achievement_sync.hpp"
#include "order_tracking.hpp"
#include "reward_event_transport.hpp"
#include "component/console/console.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "game/demonware/order_progress.hpp"
#include "steam/steam.hpp"

#include <utils/hook.hpp>
#include <deque>
#include <charconv>
#include <mutex>

namespace order_progress
{
	namespace
	{
		namespace progress = demonware::order_progress;
		using result = demonware::achievement_store::mutation_result;
		struct native_entry
		{
			std::uint32_t state;
			std::uint32_t padding;
			progress::event event;
		};
		static_assert(sizeof(native_entry) == 80);
		static_assert(offsetof(native_entry, event) == 8);
		struct native_queue
		{
			std::uint32_t capacity;
			std::uint32_t padding;
			native_entry* entries;
			std::uint64_t user;
			std::int32_t due;
			std::uint32_t tail;
		};
		static_assert(sizeof(native_queue) == 32);
		static_assert(offsetof(native_queue, user) == 16);

		struct pending_occurrence
		{
			native_queue* queue;
			std::uint64_t user;
			std::uint64_t time;
			std::vector<progress::target> targets;
			std::vector<progress::usage> usage;
			bool remote{};
		};
		std::atomic_uint64_t local_user{};
		std::atomic_bool accepting{};
		std::mutex pending_mutex;
		// Admission order matters when a failed save spans a contract deadline.
		std::deque<std::pair<native_entry*, pending_occurrence>> pending;
		struct remote_occurrence
		{
			progress::event event;
			pending_occurrence progress;
		};
		std::deque<remote_occurrence> remote_pending;
		std::uint64_t remote_stream{}, remote_sequence{};
		struct match_timer
		{
			bool running{};
			std::uint32_t start{}, seconds{};
			std::vector<progress::usage> baseline, pending;
		} timer;
		utils::hook::detour acknowledge_hook, local_acknowledge_hook, reset_hook, start_hook, stop_hook, tick_hook;
		void* start_original{};
		void* stop_original{};
		void* tick_original{};
		void* acknowledge_original{};
		void* local_acknowledge_original{};
		void* reset_original{};

		std::optional<std::string_view> cell(const game::StringTable* table, const int row, const int column)
		{
			const auto* text = table->values[row * table->columnCount + column].string;
			if (!text)
			{
				return {};
			}
			const auto length = strnlen_s(text, 129);
			return length <= 128 ? std::optional{std::string_view{text, length}} : std::nullopt;
		}

		std::vector<progress::target> matching_orders(const progress::event& event, const int event_class)
		{
			std::vector<progress::target> targets;
			const auto* table = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,
				"dw/dwGameChallenges.csv", false).stringTable;
			if (!table || !table->values || table->rowCount <= 0 || table->rowCount > 100000 ||
				table->columnCount < 5 || table->columnCount > 64)
			{
				return targets;
			}
			for (const auto& record : demonware::achievement_store::get_all())
			{
				if (!progress::eligible(record) || !demonware::achievement_kind::in_mode(record.kind, game::environment::is_zombies()))
				{
					continue;
				}
				for (int row = 0; row < table->rowCount; ++row)
				{
					if (cell(table, row, 1) != record.name)
					{
						continue;
					}
					if (cell(table, row, 2) != std::to_string(record.kind) || cell(table, row, 3) != std::to_string(event.id))
					{
						break;
					}
					const auto definition = cell(table, row, 4);
					if (!definition)
					{
						break;
					}
					bool matches{};
					if (!event_class)
					{
						const auto rule = progress::server_rule(record.name, record.kind, event.id);
						matches = definition->empty() && rule && progress::matches(*rule, event);
					}
					else
					{
						const auto id = cell(table, row, 0);
						int value{};
						if (!id)
						{
							break;
						}
						const auto parsed = std::from_chars(id->data(), id->data() + id->size(), value);
						if (parsed.ec != std::errc{} || parsed.ptr != id->data() + id->size() || value < 0)
						{
							break;
						}
						// Use the same parser as Task 5, without requiring the server's
						// private prediction cache to contain this remote player's offers.
						progress::predicate native;
						utils::hook::invoke<void>(0x139E20_g, value, &native);
						matches = progress::supported(*definition, native) && progress::matches(native, event);
					}
					if (matches)
					{
						targets.push_back({record.name, record.kind, *record.activation_timestamp, record.progress_target});
					}
					break;
				}
			}
			return targets;
		}

		bool is_relay_gameplay_event(const std::int32_t event_id)
		{
			const auto* table = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,
				"dw/dwGameEvents.csv", false).stringTable;
			if (!table || !table->values || table->columnCount < 2 ||
				table->columnCount > 64 || table->rowCount >= 10000)
			{
				return false;
			}
			const auto id = std::to_string(event_id);
			for (int row = 0; row < table->rowCount; ++row)
			{
				if (cell(table, row, 0) == id)
				{
					const auto name = cell(table, row, 1);
					return name && !name->empty() && *name != "picked_up_payroll";
				}
			}
			return false;
		}

		std::uint32_t match_clock()
		{
			// The same server clock used by AE's 0x1437E0 / 0x144390 countdown.
			return *reinterpret_cast<const std::uint32_t*>(0xA0E571C_g);
		}

		void sample_timer()
		{
			if (!timer.running || remote_stream)
			{
				return;
			}
			const auto seconds = (match_clock() - timer.start) / 1000;
			if (seconds == timer.seconds)
			{
				return;
			}
			timer.seconds = seconds;
			timer.pending = timer.baseline;
			for (auto& value : timer.pending)
			{
				value.remaining -= static_cast<std::int32_t>(std::min(seconds, static_cast<std::uint32_t>(value.remaining)));
			}
		}

		void flush()
		{
			if (pending.empty() && timer.pending.empty())
			{
				return;
			}
			while (!pending.empty())
			{
				const auto& [entry, occurrence] = pending.front();
				if ((occurrence.remote || occurrence.queue->user == occurrence.user) && entry->state)
				{
					const auto saved = progress::settle(occurrence.targets, occurrence.time, occurrence.usage);
					if (saved == result::save_failed)
					{
						return;
					}
					pending.pop_front(); // Forget before a post-commit refresh can throw.
					if (saved == result::updated)
					{
						achievement_sync::request_refresh();
					}
				}
				else
				{
					pending.pop_front();
				}
			}
			if (!remote_pending.empty())
			{
				return; // Preserve event/deadline ordering while the native queue is full.
			}
			// Absolute remaining values are safe to retry after stop, reconnect or a
			// failed end_mission response. Transport never subtracts mission time again.
			const auto saved = progress::settle({}, static_cast<std::uint64_t>(time(nullptr)), timer.pending);
			if (saved == result::save_failed)
			{
				return;
			}
			if (saved == result::updated && std::ranges::any_of(timer.pending,
				[](const auto& value) { return value.remaining == 0; }))
			{
				achievement_sync::request_refresh();
			}
			if (!timer.running)
			{
				timer.pending.clear();
			}
			const auto active = demonware::achievement_store::get_all();
			const auto settled = [&](const auto& value)
			{
				return std::ranges::none_of(active, [&](const auto& record)
				{
					return record.name == value.achievement.name && progress::eligible(record) &&
						record.activation_timestamp == value.achievement.activation;
				});
			};
			std::erase_if(timer.baseline, settled);
			std::erase_if(timer.pending, settled);
		}

		void admit(native_queue* queue, native_entry* entry, const int event_class) noexcept
		{
			try
			{
				std::lock_guard lock{pending_mutex};
				// A reused slot is a distinct occurrence, including identical payloads.
				std::erase_if(pending, [=](const auto& value) { return value.first == entry; });
				if (!accepting || (event_class != 0 && event_class != 1) || !queue || !entry || entry->state != 1 || !queue->user)
				{
					return;
				}
				if (queue->user != local_user.load())
				{
					reward_event_relay::admit(queue->user, entry->event, event_class);
					return;
				}
				auto targets = matching_orders(entry->event, event_class);
				sample_timer();
				if (!targets.empty())
				{
					pending.emplace_back(entry, pending_occurrence{queue, queue->user,
						static_cast<std::uint64_t>(time(nullptr)), std::move(targets), timer.running ? timer.pending : std::vector<progress::usage>{}});
				}
				flush();
			}
			catch (const std::exception& error)
			{
				console::error("Order admission failed: %s\n", error.what());
			}
		}

		void drain_remote()
		{
			if (remote_pending.empty())
			{
				return;
			}
			const auto controller = game::CL_ControllerIndexFromClientNum(0);
			if (controller < 0 || controller >= 2)
			{
				return;
			}
			auto* queue = reinterpret_cast<native_queue*>(0x60A4040_g) + controller;
			if (!queue->entries || queue->capacity != 40)
			{
				return;
			}
			// Reward_GameEventComplexNotification (0x124180) copies this exact
			// payload into the controller's queue. Task 12 owns submission/retry and
			// the existing quest/hidden/HQ consumers; do not duplicate them here.
			for (unsigned i = 0; i < queue->capacity && !remote_pending.empty(); ++i)
			{
				auto* entry = &queue->entries[i];
				if (entry->state)
				{
					continue;
				}
				auto value = std::move(remote_pending.front());
				remote_pending.pop_front();
				entry->event = value.event;
				entry->state = 1;
				queue->due = 0;
				value.progress.queue = queue;
				pending.emplace_back(entry, std::move(value.progress));
			}
		}

		void recover() noexcept
		{
			std::lock_guard lock{pending_mutex};
			if (!accepting)
			{
				return;
			}
			try
			{
				drain_remote();
				flush();
			}
			catch (const std::exception& error)
			{
				console::error("Order save retry failed: %s\n", error.what());
			}
		}

		void retry() noexcept
		{
			std::lock_guard lock{pending_mutex};
			if (!accepting)
			{
				return;
			}
			try
			{
				drain_remote();
				flush();
			}
			catch (const std::exception& error)
			{
				console::error("Order save retry failed: %s\n", error.what());
			}
			// Native success clears state 2 and retains state 1. Preserve every unsaved
			// entry, including later occurrences held behind a failed earlier save.
			for (const auto& [entry, occurrence] : pending)
			{
				if (entry->state == 2 && (occurrence.remote || occurrence.queue->user == occurrence.user))
				{
					entry->state = 1;
				}
			}
		}

		void begin_timer()
		{
			// Carry an unsaved previous match's absolute balance into the next match.
			std::vector<progress::usage> baseline;
			for (const auto& record : demonware::achievement_store::get_all())
			{
				if (!progress::timed(record) || !progress::eligible(record) ||
					!demonware::achievement_kind::in_mode(record.kind, game::environment::is_zombies()))
				{
					continue;
				}
				auto remaining = *record.usage_time_remaining;
				for (const auto& value : timer.pending)
				{
					if (value.achievement.name == record.name && value.achievement.activation == record.activation_timestamp)
					{
						remaining = std::min(remaining, value.remaining);
					}
				}
				baseline.push_back({{record.name, record.kind, *record.activation_timestamp, record.progress_target}, remaining});
			}
			timer = {true, match_clock(), 0, baseline, baseline};
		}

		void start(std::uint64_t user) noexcept
		{
			try
			{
				std::lock_guard lock{pending_mutex};
				if (!accepting)
				{
					return;
				}
				if (user != local_user.load())
				{
					reward_event_relay::start(user);
					return;
				}
				begin_timer();
			}
			catch (const std::exception& error)
			{
				console::error("Contract timer start failed: %s\n", error.what());
			}
		}

		void tick() noexcept
		{
			try
			{
				std::lock_guard lock{pending_mutex};
				if (!accepting)
				{
					return;
				}
				reward_event_relay::tick();
				if (!timer.running || remote_stream)
				{
					return;
				}
				const auto previous = timer.seconds;
				sample_timer();
				if (timer.seconds != previous)
				{
					flush();
				}
			}
			catch (const std::exception& error)
			{
				console::error("Contract timer save failed: %s\n", error.what());
			}
		}

		void stop(std::uint64_t user) noexcept
		{
			try
			{
				std::lock_guard lock{pending_mutex};
				if (!accepting)
				{
					return;
				}
				if (user != local_user.load())
				{
					reward_event_relay::stop(user);
					return;
				}
				if (!timer.running || remote_stream)
				{
					return;
				}
				sample_timer();
				timer.running = false;
				flush();
				achievement_sync::request_refresh();
			}
			catch (const std::exception& error)
			{
				console::error("Contract timer stop failed: %s\n", error.what());
			}
		}

		void reset() noexcept
		{
			std::lock_guard lock{pending_mutex};
			reward_event_relay::reset();
			std::erase_if(pending, [](const auto& value) { return !value.second.remote; });
			if (!remote_stream)
			{
				timer.running = false; // Never advance a stopped clock in a lobby or new map.
			}
			// Keep the last sampled balance for a save retry after native teardown.
		}

		void save_registers(utils::hook::assembler& a)
		{
			a.pushad64();
			a.sub(rsp, 0x60);
			for (unsigned i = 0; i < 6; ++i)
			{
				a.movdqu(xmmword_ptr(rsp, i * 16), asmjit::x86::xmm(i));
			}
		}

		void restore_registers(utils::hook::assembler& a)
		{
			for (unsigned i = 0; i < 6; ++i)
			{
				a.movdqu(asmjit::x86::xmm(i), xmmword_ptr(rsp, i * 16));
			}
			a.add(rsp, 0x60);
			a.popad64();
		}

		template <typename Callback>
		void* observer(Callback callback, void** original)
		{
			return utils::hook::assemble([=](utils::hook::assembler& a)
			{
				// These optimized native functions preserve volatile registers beyond
				// the usual C++ ABI. Restore their inputs before tail-calling the original.
				save_registers(a);
				a.call_aligned(callback);
				restore_registers(a);
				a.push(rax);
				a.mov(rax, reinterpret_cast<std::uintptr_t>(original));
				a.mov(rax, qword_ptr(rax));
				a.xchg(qword_ptr(rsp), rax);
				a.ret();
			});
		}
	}

	void receive(const demonware::reward_event_relay::batch& batch)
	{
		namespace wire = demonware::reward_event_relay;
		if (!accepting || !local_user || batch.user != local_user || batch.zombies != game::environment::is_zombies())
		{
			return;
		}
		// Native reliable commands are ordered within a connection; CL_Disconnect
		// retires the receiver. Only the first record may establish a new stream.
		if (batch.records.empty())
		{
			return;
		}
		std::lock_guard lock{pending_mutex};
		if (batch.stream != remote_stream)
		{
			if (batch.records.front().sequence != 1)
			{
				return;
			}
			remote_stream = batch.stream;
			remote_sequence = 0;
			timer.running = false;
		}
		for (const auto& value : batch.records)
		{
			if (value.sequence <= remote_sequence)
			{
				continue;
			}
			remote_sequence = value.sequence;
			if (value.type == wire::operation::start)
			{
				begin_timer(); // Same activation snapshots and unsaved-time carryover.
				continue;
			}
			if (timer.running && value.seconds >= timer.seconds)
			{
				timer.seconds = value.seconds;
				timer.pending = timer.baseline;
				for (auto& usage : timer.pending)
				{
					usage.remaining -= static_cast<std::int32_t>(std::min(value.seconds, static_cast<std::uint32_t>(usage.remaining)));
				}
			}
			if (value.type == wire::operation::event && remote_pending.size() < wire::pending_limit)
			{
				// Resolve names from stock data, never trust arbitrary server strings
				// as a local economy action. The native ID and every selector survive.
				if (is_relay_gameplay_event(value.event.id))
				{
					remote_pending.push_back({value.event, {nullptr, batch.user, static_cast<std::uint64_t>(time(nullptr)),
						matching_orders(value.event, value.event_class), timer.running ? timer.pending : std::vector<progress::usage>{}, true}});
				}
			}
			if (value.type == wire::operation::stop)
			{
				timer.running = false;
			}
			drain_remote();
			flush();
		}
	}

	void disconnect()
	{
		std::lock_guard lock{pending_mutex};
		if (!remote_stream)
		{
			return;
		}
		try
		{
			drain_remote();
			flush();
		}
		catch (const std::exception& error)
		{
			console::error("Order disconnect save failed: %s\n", error.what());
		}
		remote_pending.clear();
		std::erase_if(pending, [](const auto& value) { return value.second.remote; });
		remote_stream = remote_sequence = 0;
		timer.running = false; // Retry the last absolute observation, never extrapolate offline time.
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			accepting = true;
			if (!game::environment::is_dedicated())
			{
				scheduler::once([] { local_user = steam::SteamUser()->GetSteamID().bits; }, scheduler::pipeline::main);
			}
			const auto admission = utils::hook::assemble([](utils::hook::assembler& a)
			{
				save_registers(a);
				a.mov(r8d, edx);
				a.mov(rdx, rax); // Complete entry; RCX is its owning queue.
				a.call_aligned(admit);
				restore_registers(a);
				a.jmp(0x13E5D0_g);
			});
			for (const auto call : {0x137D11_g, 0x1382AE_g, 0x143051_g})
			{
				utils::hook::call(call, admission);
			}
			acknowledge_hook.create(0x1438D0_g, observer(retry, &acknowledge_original));
			acknowledge_original = acknowledge_hook.get_original();
			local_acknowledge_hook.create(0x13C3E0_g, observer(retry, &local_acknowledge_original));
			local_acknowledge_original = local_acknowledge_hook.get_original();
			reset_hook.create(0x142AD0_g, observer(reset, &reset_original));
			reset_original = reset_hook.get_original();
			// lootservicestarttrackingplaytime starts both the native AE countdown and
			// the mission clock. Game-end and ClientDisconnect stop the latter here.
			start_hook.create(0x1437E0_g, observer(start, &start_original));
			start_original = start_hook.get_original();
			stop_hook.create(0x2B22C0_g, observer(stop, &stop_original));
			stop_original = stop_hook.get_original();
			tick_hook.create(0x144390_g, observer(tick, &tick_original));
			tick_original = tick_hook.get_original();
			if (!game::environment::is_dedicated())
			{
				scheduler::loop(recover, scheduler::pipeline::main, 1s);
			}
		}

		void pre_destroy() override
		{
			accepting = false;
			reset();
		}
	};
}

REGISTER_COMPONENT(order_progress::component)
