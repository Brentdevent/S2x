#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "component/scheduler.hpp"

#include "game/demonware/loot_catalog_engine.hpp"
#include "game/demonware/loot_catalog_lifecycle.hpp"
#include "game/demonware/runtime_context.hpp"
#include "game/game.hpp"
#include "steam/steam.hpp"

#include <utils/hook.hpp>
#include <utils/finally.hpp>

namespace demonware_runtime
{
	namespace
	{
		std::atomic_bool accepting_work{};
		std::mutex publication_mutex{};
		std::uint32_t producer_thread{};
		const game::dvar_t* loot_rarity_scale{};
		utils::hook::detour database_load_hook;
		utils::hook::detour database_teardown_hook;

		bool is_producer_thread()
		{
			const auto thread_id = GetCurrentThreadId();
			// S2 MP Sys_IsMainThread (0x674D60) compares GetCurrentThreadId
			// with its main-thread scalar at 0xAC92570. Never infer this from TLS.
			const auto verified = !game::environment::uses_multiplayer_binary() ||
				utils::hook::invoke<bool>(0x674D60_g);
			assert(verified && (!producer_thread || producer_thread == thread_id));
			if (!verified || (producer_thread && producer_thread != thread_id))
			{
				return false;
			}
			producer_thread = thread_id;
			return true;
		}

		void publish_identity()
		{
			if (!accepting_work.load(std::memory_order_acquire) || !is_producer_thread())
			{
				return;
			}

			auto current = demonware::runtime_context::get_snapshot();
			// Capture the Steam/S2x identity once. Persona text may change through
			// the stock name dvar, so only its owned string is refreshed on main.
			const auto user_id = current ? current->user_id : steam::SteamUser()->GetSteamID().bits;
			const auto* name = steam::SteamFriends()->GetPersonaName();
			const auto name_length = name ? strnlen_s(name, 256) : 0;
			if (name_length == 256)
			{
				return;
			}
			const std::string persona = name_length ? std::string{name, name_length} : "S2x";
			// Reuse the existing main-thread publication: workers never read dvars.
			const auto rarity_scale = loot_rarity_scale ? loot_rarity_scale->current.value : 1.0f;
			if (current && current->persona_name == persona && current->loot_rarity_scale == rarity_scale)
			{
				return;
			}
			std::lock_guard publication_lock{publication_mutex};
			if (!accepting_work.load(std::memory_order_acquire))
			{
				return;
			}
			demonware::runtime_context::publish({user_id, persona, producer_thread, rarity_scale});
		}

		bool database_ready()
		{
			// scheduler::main runs AFTER stock 0x92390. Its 0x923AD..0x923CB
			// gate starts postload while the completion byte is zero. Postload's
			// finalizer 0xA62B0 sets it to one at 0xA6377; require that finished
			// state here, not the pre-postload condition (also see DB_SyncXAsset).
			const demonware::loot_catalog_engine::database_state state{
				*reinterpret_cast<const int*>(0x27D1098_g) != 0,
				*reinterpret_cast<const std::uint8_t*>(0x27CEC67_g) != 0,
				utils::hook::invoke<bool>(0x674D40_g)};
			return demonware::loot_catalog_engine::ready_for_snapshot(state);
		}

		void refresh_loot_catalog()
		{
			if (!accepting_work.load(std::memory_order_acquire) || !is_producer_thread())
			{
				return;
			}
			demonware::runtime::loot_lifecycle().poll(database_ready, [](const std::uint64_t revision)
			{
				return demonware::loot_catalog_engine::build_catalog(revision, producer_thread);
			});
		}

		void database_load_stub(game::XZoneInfo* zones, const unsigned count, const game::DBSyncMode mode)
		{
			// S2 DB_LoadXAssets 0xA4F60 includes unload-only and transient loads.
			// Invalidate BEFORE it can remove assets. Some down/up cycles finish
			// between main polls; a scalar readiness edge alone would miss them.
			auto& lifecycle = demonware::runtime::loot_lifecycle();
			lifecycle.begin_load();
			const auto finish = utils::finally([&lifecycle] { lifecycle.end_load(); });
			database_load_hook.invoke<void>(zones, count, mode);
		}

		std::uintptr_t database_teardown_stub()
		{
			// AA610 tears down the whole DB on renderer shutdown / quit. Enter
			// before its writer lock (AA67F), not in the inner ADE50 logger hook.
			auto& lifecycle = demonware::runtime::loot_lifecycle();
			lifecycle.begin_load();
			const auto finish = utils::finally([&lifecycle] { lifecycle.end_load(); });
			return database_teardown_hook.invoke<std::uintptr_t>();
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::uses_multiplayer_binary() && !game::environment::is_dedicated())
			{
				loot_rarity_scale = game::Dvar_RegisterFloat("cg_lootRarityScale", 1.0f, 0.5f, 3.0f, game::DVAR_FLAG_SAVED);
			}
			accepting_work.store(true, std::memory_order_release);
			// Identity is independent of DB/loot readiness and survives map changes.
			scheduler::loop(publish_identity, scheduler::pipeline::main);
			if (game::environment::uses_multiplayer_binary() && !game::environment::is_dedicated())
			{
				database_load_hook.create(game::DB_LoadXAssets, database_load_stub);
				database_teardown_hook.create(0xAA610_g, database_teardown_stub);
				// Only stable scalar/event checks recur. The provider executes once
				// for each invalidated, ready generation, never to detect changes.
				scheduler::loop(refresh_loot_catalog, scheduler::pipeline::main);
			}
		}

		void pre_destroy() override
		{
			std::lock_guard publication_lock{publication_mutex};
			accepting_work.store(false, std::memory_order_release);
			demonware::runtime::loot_lifecycle().stop();
			demonware::runtime_context::clear();
			// Do not touch producer-owned scratch here: shutdown can run off-main.
		}
	};
}

REGISTER_COMPONENT(demonware_runtime::component)
