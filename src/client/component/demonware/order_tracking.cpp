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

#include <charconv>
#include <deque>
#include <mutex>

namespace order_progress
{
	namespace
	{
		namespace progress = demonware::order_progress;
		namespace wire = demonware::reward_event_relay;

		using mutation_result = demonware::achievement_store::mutation_result;

		constexpr unsigned native_queue_capacity = 40;
		constexpr int native_controller_count = 2;
		constexpr std::uint32_t timer_save_interval_seconds = 30;

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

		struct remote_occurrence
		{
			progress::event event;
			pending_occurrence progress;
		};

		struct relayed_occurrence
		{
			native_queue* queue;
			std::uint64_t user;
			reward_event_relay::delivery delivery;
		};

		struct match_timer
		{
			bool running{};
			std::uint32_t start{};
			std::uint32_t seconds{};
			std::vector<progress::usage> baseline;
			std::vector<progress::usage> pending;
			std::uint32_t checkpoint_seconds{};
		};

		std::atomic_uint64_t local_user{};
		std::atomic_bool accepting{};
		std::mutex pending_mutex;

		// Admission order matters when a failed save spans a contract deadline
		std::deque<std::pair<native_entry*, pending_occurrence>> pending;
		std::deque<remote_occurrence> remote_pending;
		std::unordered_map<native_entry*, relayed_occurrence> relayed;
		std::uint64_t remote_stream{};
		std::uint64_t remote_sequence{};
		match_timer timer;

		utils::hook::detour acknowledge_hook;
		utils::hook::detour local_acknowledge_hook;
		utils::hook::detour reset_hook;
		utils::hook::detour start_hook;
		utils::hook::detour stop_hook;
		utils::hook::detour tick_hook;

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
				return std::nullopt;
			}

			const auto length = strnlen_s(text, 129);
			if (length > 128)
			{
				return std::nullopt;
			}

			return std::string_view{text, length};
		}

		bool parse_native_id(const std::string_view text, int& value)
		{
			const auto end = text.data() + text.size();
			const auto parsed = std::from_chars(text.data(), end, value);
			return parsed.ec == std::errc{} && parsed.ptr == end && value >= 0;
		}

		bool is_valid_challenge_table(const game::StringTable* table)
		{
			return table && table->values && table->rowCount > 0 && table->rowCount <= 100000 &&
				table->columnCount >= 5 && table->columnCount <= 64;
		}

		std::optional<int> find_challenge_row(const game::StringTable* table, const std::string& name)
		{
			for (int row = 0; row < table->rowCount; ++row)
			{
				if (cell(table, row, 1) == name)
				{
					return row;
				}
			}

			return std::nullopt;
		}

		std::string_view special_order_unit(const demonware::achievement_record& record)
		{
			if (!demonware::achievement_kind::special(record.kind))
			{
				return {};
			}

			const auto* table = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,
				"mp/periodicChallengeTable.csv", false).stringTable;
			if (!is_valid_challenge_table(table) || table->columnCount <= 9)
			{
				return {};
			}

			for (int row = 0; row < table->rowCount; ++row)
			{
				if (cell(table, row, 2) == record.name)
				{
					return cell(table, row, 9).value_or(std::string_view{});
				}
			}

			return {};
		}

		bool challenge_row_matches(const game::StringTable* table, const int row,
			const demonware::achievement_record& record, const progress::event& event, const int event_class)
		{
			if (cell(table, row, 2) != std::to_string(record.kind) || cell(table, row, 3) != std::to_string(event.id))
			{
				return false;
			}

			const auto definition = cell(table, row, 4);
			if (!definition)
			{
				return false;
			}

			if (!event_class)
			{
				const auto rule = progress::server_rule(record.name, record.kind, event.id, special_order_unit(record));
				return definition->empty() && rule && progress::matches(*rule, event);
			}

			const auto id = cell(table, row, 0);
			int value{};
			if (!id || !parse_native_id(*id, value))
			{
				return false;
			}

			// The native parser does not need the server prediction cache to hold this remote player's offers
			progress::predicate native;
			utils::hook::invoke<void>(0x139E20_g, value, &native);
			return progress::supported(*definition, native) && progress::matches(native, event);
		}

		std::vector<progress::target> matching_orders(const progress::event& event, const int event_class)
		{
			std::vector<progress::target> targets;

			const auto* table = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,
				"dw/dwGameChallenges.csv", false).stringTable;
			if (!is_valid_challenge_table(table))
			{
				return targets;
			}

			for (const auto& record : demonware::achievement_store::get_all())
			{
				if (!progress::eligible(record) ||
					!demonware::achievement_kind::in_mode(record.kind, game::environment::is_zombies()))
				{
					continue;
				}

				const auto row = find_challenge_row(table, record.name);
				if (row && challenge_row_matches(table, *row, record, event, event_class))
				{
					targets.push_back({record.name, record.kind, *record.activation_timestamp, record.progress_target});
				}
			}

			return targets;
		}

		bool is_valid_events_table(const game::StringTable* table)
		{
			return table && table->values && table->columnCount >= 2 &&
				table->columnCount <= 64 && table->rowCount < 10000;
		}

		bool is_relay_gameplay_event(const std::int32_t event_id)
		{
			const auto* table = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE,
				"dw/dwGameEvents.csv", false).stringTable;
			if (!is_valid_events_table(table))
			{
				return false;
			}

			const auto id = std::to_string(event_id);
			for (int row = 0; row < table->rowCount; ++row)
			{
				if (cell(table, row, 0) != id)
				{
					continue;
				}

				const auto name = cell(table, row, 1);
				return name && !name->empty() && *name != "picked_up_payroll";
			}

			return false;
		}

		std::uint32_t match_clock()
		{
			// Same server clock as the AE countdown at 0x1437E0 / 0x144390
			return *reinterpret_cast<const std::uint32_t*>(0xA0E571C_g);
		}

		std::vector<progress::usage> current_usage()
		{
			return timer.running ? timer.pending : std::vector<progress::usage>{};
		}

		void apply_elapsed_seconds(const std::uint32_t seconds)
		{
			timer.seconds = seconds;
			timer.pending = timer.baseline;

			for (auto& value : timer.pending)
			{
				const auto elapsed = std::min(seconds, static_cast<std::uint32_t>(value.remaining));
				value.remaining -= static_cast<std::int32_t>(elapsed);
			}
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

			apply_elapsed_seconds(seconds);
		}

		bool flush_occurrences()
		{
			while (!pending.empty())
			{
				const auto& [entry, occurrence] = pending.front();

				const auto owned = !entry || occurrence.remote || occurrence.queue->user == occurrence.user;
				if (!owned || (entry && !entry->state))
				{
					pending.pop_front();
					continue;
				}

				const auto saved = progress::settle(occurrence.targets, occurrence.time, occurrence.usage);
				if (saved == mutation_result::save_failed)
				{
					return false;
				}

				// Forget before a post-commit refresh can throw
				pending.pop_front();

				if (saved == mutation_result::updated)
				{
					achievement_sync::request_refresh();
				}
			}

			return true;
		}

		void settle_timer()
		{
			if (timer.pending.empty())
			{
				return;
			}

			const auto any_expired = std::ranges::any_of(timer.pending, [](const auto& value)
			{
				return value.remaining == 0;
			});

			// Coalesce timer-only saves. Objective progress carries its own usage
			// snapshot; stopping or expiring a contract must still settle immediately.
			if (timer.running && !any_expired &&
				timer.seconds - timer.checkpoint_seconds < timer_save_interval_seconds)
			{
				return;
			}

			// Absolute remaining values make retries after stop or reconnect safe
			const auto saved = progress::settle({}, static_cast<std::uint64_t>(time(nullptr)), timer.pending);
			if (saved == mutation_result::save_failed)
			{
				return;
			}

			timer.checkpoint_seconds = timer.seconds;

			if (saved == mutation_result::updated && any_expired)
			{
				achievement_sync::request_refresh();
			}

			if (!timer.running)
			{
				timer.pending.clear();
			}

			const auto active = demonware::achievement_store::get_all();
			const auto is_settled = [&](const auto& value)
			{
				return std::ranges::none_of(active, [&](const auto& record)
				{
					return record.name == value.achievement.name && progress::eligible(record) &&
						record.activation_timestamp == value.achievement.activation;
				});
			};

			std::erase_if(timer.baseline, is_settled);
			std::erase_if(timer.pending, is_settled);
		}

		void flush()
		{
			if (pending.empty() && timer.pending.empty())
			{
				return;
			}

			if (!flush_occurrences())
			{
				return;
			}

			// Preserve event and deadline ordering while the native queue is full
			if (!remote_pending.empty())
			{
				return;
			}

			settle_timer();
		}

		bool is_admissible(const native_queue* queue, const native_entry* entry, const int event_class)
		{
			return accepting && (event_class == 0 || event_class == 1) && queue && entry &&
				entry->state == 1 && queue->user;
		}

		void admit(native_queue* queue, native_entry* entry, const int event_class) noexcept
		{
			try
			{
				std::lock_guard lock{pending_mutex};

				// A reused slot is a distinct occurrence, including identical payloads
				std::erase_if(pending, [=](const auto& value)
				{
					return value.first == entry;
				});

				relayed.erase(entry);

				if (!is_admissible(queue, entry, event_class))
				{
					return;
				}

				if (queue->user != local_user.load())
				{
					const auto delivery = reward_event_relay::admit(queue->user, entry->event, event_class);
					if (delivery.sequence)
					{
						relayed.emplace(entry, relayed_occurrence{queue, queue->user, delivery});
					}

					return;
				}

				auto targets = matching_orders(entry->event, event_class);
				sample_timer();

				if (!targets.empty())
				{
					pending.emplace_back(entry, pending_occurrence{queue, queue->user,
						static_cast<std::uint64_t>(time(nullptr)), std::move(targets), current_usage()});
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
			if (controller < 0 || controller >= native_controller_count)
			{
				return;
			}

			auto* queue = reinterpret_cast<native_queue*>(0x60A4040_g) + controller;
			if (!queue->entries || queue->capacity != native_queue_capacity)
			{
				return;
			}

			// Reward_GameEventComplexNotification (0x124180) copies this payload into the controller queue
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

				if (!value.progress.targets.empty())
				{
					value.progress.queue = queue;
					pending.emplace_back(entry, std::move(value.progress));
				}
			}
		}

		void retry_pending_saves()
		{
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

		void recover() noexcept
		{
			std::lock_guard lock{pending_mutex};
			if (!accepting)
			{
				return;
			}

			retry_pending_saves();
		}

		void retry() noexcept
		{
			std::lock_guard lock{pending_mutex};
			if (!accepting)
			{
				return;
			}

			retry_pending_saves();

			for (auto it = relayed.begin(); it != relayed.end();)
			{
				auto* entry = it->first;
				const auto& occurrence = it->second;
				if (occurrence.queue->user != occurrence.user || !entry->state ||
					!reward_event_relay::is_pending(occurrence.user, occurrence.delivery))
				{
					it = relayed.erase(it);
					continue;
				}

				// Task 11 success must not retire an occurrence the remote client has not accepted.
				if (entry->state == 2)
				{
					entry->state = 1;
				}

				++it;
			}

			// Native success clears state 2 and retains state 1, so keep every unsaved entry
			for (const auto& [entry, occurrence] : pending)
			{
				if (entry && entry->state == 2 && (occurrence.remote || occurrence.queue->user == occurrence.user))
				{
					entry->state = 1;
				}
			}
		}

		void begin_timer()
		{
			// Carry an unsaved previous match's absolute balance into the next match
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

		void start(const std::uint64_t user) noexcept
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

		void stop(const std::uint64_t user) noexcept
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
			relayed.clear();

			// The native reset reuses server queue slots. Keep failed local saves
			// in admission order, without retaining pointers into those queues.
			for (auto& [entry, occurrence] : pending)
			{
				if (!occurrence.remote)
				{
					entry = nullptr;
					occurrence.queue = nullptr;
				}
			}

			if (!remote_stream)
			{
				timer.running = false;
			}
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
				// These optimized native functions preserve volatile registers beyond the C++ ABI
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

		template <typename Callback>
		void install_observer(utils::hook::detour& hook, const std::size_t address, Callback callback, void*& original)
		{
			hook.create(address, observer(callback, &original));
			original = hook.get_original();
		}

		bool begin_remote_stream(const wire::batch& batch)
		{
			if (batch.stream == remote_stream)
			{
				return true;
			}

			// Reliable commands are ordered per connection, so only a first record may open a new stream
			if (batch.records.front().sequence != 1)
			{
				return false;
			}

			remote_stream = batch.stream;
			remote_sequence = 0;
			timer.running = false;
			return true;
		}

		void queue_remote_event(const wire::batch& batch, const wire::record& value)
		{
			// Event names come from stock data, never from arbitrary server strings
			if (!is_relay_gameplay_event(value.event.id))
			{
				return;
			}

			pending_occurrence occurrence{nullptr, batch.user, static_cast<std::uint64_t>(time(nullptr)),
				matching_orders(value.event, value.event_class), current_usage(), true};
			remote_pending.push_back({value.event, std::move(occurrence)});
		}

		bool apply_remote_record(const wire::batch& batch, const wire::record& value)
		{
			if (value.type == wire::operation::event && pending.size() + remote_pending.size() >= wire::pending_limit)
			{
				return false;
			}

			if (value.type == wire::operation::start)
			{
				begin_timer();
				return true;
			}

			if (timer.running && value.seconds >= timer.seconds)
			{
				apply_elapsed_seconds(value.seconds);
			}

			if (value.type == wire::operation::event)
			{
				queue_remote_event(batch, value);
			}

			if (value.type == wire::operation::stop)
			{
				timer.running = false;
			}

			return true;
		}
	}

	std::optional<std::uint64_t> receive(const wire::batch& batch)
	{
		if (!accepting || !local_user || batch.user != local_user || batch.zombies != game::environment::is_zombies())
		{
			return {};
		}

		if (batch.records.empty())
		{
			return {};
		}

		std::lock_guard lock{pending_mutex};
		if (!begin_remote_stream(batch))
		{
			return {};
		}

		for (const auto& value : batch.records)
		{
			if (value.sequence <= remote_sequence)
			{
				continue;
			}

			// Accept only a contiguous prefix. A full queue leaves the rest on the sender.
			if (value.sequence != remote_sequence + 1 || !apply_remote_record(batch, value))
			{
				break;
			}

			remote_sequence = value.sequence;
			drain_remote();
			flush();
		}

		return remote_sequence;
	}

	void disconnect()
	{
		std::lock_guard lock{pending_mutex};
		if (!remote_stream)
		{
			return;
		}

		// Native slots can be reset or reused after disconnect. Retain only the
		// copied settlement data so failed saves can recover in admission order.
		for (auto& [entry, occurrence] : pending)
		{
			if (occurrence.remote)
			{
				entry = nullptr;
				occurrence.queue = nullptr;
			}
		}

		// Keep payloads for native Task 12 delivery after the queue has room.
		// Detach their progress so later delivery cannot settle it a second time.
		for (auto& value : remote_pending)
		{
			if (!value.progress.targets.empty())
			{
				pending.emplace_back(nullptr, std::move(value.progress));
				value.progress = {};
			}
		}

		// Keep the final reported time after its events, even if a new match
		// replaces the timer before persistence recovers.
		if (!timer.pending.empty())
		{
			pending.emplace_back(nullptr, pending_occurrence{nullptr, local_user,
				static_cast<std::uint64_t>(time(nullptr)), {}, timer.pending, true});
		}

		remote_stream = 0;
		remote_sequence = 0;
		timer.running = false;

		try
		{
			flush();
		}
		catch (const std::exception& error)
		{
			console::error("Order disconnect save failed: %s\n", error.what());
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			accepting = true;

			if (!game::environment::is_dedicated())
			{
				scheduler::once([]
				{
					local_user = steam::SteamUser()->GetSteamID().bits;
				}, scheduler::pipeline::main);
			}

			const auto admission = utils::hook::assemble([](utils::hook::assembler& a)
			{
				save_registers(a);
				a.mov(r8d, edx);

				// RCX is the owning queue, RAX the complete entry
				a.mov(rdx, rax);
				a.call_aligned(admit);

				restore_registers(a);
				a.jmp(0x13E5D0_g);
			});

			for (const auto call : {0x137D11_g, 0x1382AE_g, 0x143051_g})
			{
				utils::hook::call(call, admission);
			}

			install_observer(acknowledge_hook, 0x1438D0_g, retry, acknowledge_original);
			install_observer(local_acknowledge_hook, 0x13C3E0_g, retry, local_acknowledge_original);
			install_observer(reset_hook, 0x142AD0_g, reset, reset_original);

			// lootservicestarttrackingplaytime starts both the native AE countdown and the mission clock
			install_observer(start_hook, 0x1437E0_g, start, start_original);
			install_observer(stop_hook, 0x2B22C0_g, stop, stop_original);
			install_observer(tick_hook, 0x144390_g, tick, tick_original);

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
