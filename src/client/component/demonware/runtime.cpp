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
		constexpr std::size_t max_persona_length = 256;

		std::atomic_bool accepting_work{};
		std::mutex publication_mutex{};
		std::uint32_t producer_thread{};
		const game::dvar_t* loot_rarity_scale{};

		utils::hook::detour database_load_hook;
		utils::hook::detour database_teardown_hook;

		bool is_loot_enabled()
		{
			return game::environment::uses_multiplayer_binary() && !game::environment::is_dedicated();
		}

		bool is_producer_thread()
		{
			const auto thread_id = GetCurrentThreadId();

			// Sys_IsMainThread (0x674D60) compares GetCurrentThreadId with the main thread id at 0xAC92570
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

		std::optional<demonware::runtime_context::zombie_rank> read_zombie_rank()
		{
			if (game::environment::is_dedicated() || !game::environment::is_zombies())
			{
				return {};
			}

			const auto controller = game::CL_ControllerIndexFromClientNum(0);
			if (controller < 0 || !game::LiveStorage_DoWeHaveStats(controller))
			{
				return {};
			}

			// CoD.StatsGroup.Coop; read on the main thread before publishing to DW.
			static const auto prestige = game::DDL_HashString("prestigeLevel");
			static const auto experience = game::DDL_HashString("totalXP");
			return demonware::runtime_context::zombie_rank{
				game::LiveStorage_PlayerDataGetIntByNameArray(controller, &prestige, 1, 3),
				game::LiveStorage_PlayerDataGetIntByNameArray(controller, &experience, 1, 3)};
		}

		void publish_identity()
		{
			if (!accepting_work.load(std::memory_order_acquire) || !is_producer_thread())
			{
				return;
			}

			const auto current = demonware::runtime_context::get_snapshot();
			const auto user_id = current ? current->user_id : steam::SteamUser()->GetSteamID().bits;

			const auto* name = steam::SteamFriends()->GetPersonaName();
			const auto name_length = name ? strnlen_s(name, max_persona_length) : 0;
			if (name_length == max_persona_length)
			{
				return;
			}

			const std::string persona = name_length ? std::string{name, name_length} : "S2x";
			const auto rarity_scale = loot_rarity_scale ? loot_rarity_scale->current.value : 1.0f;
			const auto zombies = read_zombie_rank();
			if (current && current->persona_name == persona && current->loot_rarity_scale == rarity_scale &&
				current->zombies == zombies)
			{
				return;
			}

			std::lock_guard publication_lock{publication_mutex};
			if (!accepting_work.load(std::memory_order_acquire))
			{
				return;
			}

			demonware::runtime_context::publish({user_id, persona, producer_thread, rarity_scale, zombies});
		}

		bool database_ready()
		{
			// Ready means postload has finished: the finalizer 0xA62B0 sets the completion byte at 0xA6377
			const demonware::loot_catalog_engine::database_state state{
				*reinterpret_cast<const int*>(0x27D1098_g) != 0,
				*reinterpret_cast<const std::uint8_t*>(0x27CEC67_g) != 0,
				utils::hook::invoke<bool>(0x674D40_g),
			};

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
			// DB_LoadXAssets (0xA4F60) also covers unload-only and transient loads, so invalidate before it runs
			auto& lifecycle = demonware::runtime::loot_lifecycle();
			lifecycle.begin_load();

			const auto finish = utils::finally([&lifecycle]
			{
				lifecycle.end_load();
			});

			database_load_hook.invoke<void>(zones, count, mode);
		}

		std::uintptr_t database_teardown_stub()
		{
			// 0xAA610 tears down the whole database, enter before its writer lock at 0xAA67F
			auto& lifecycle = demonware::runtime::loot_lifecycle();
			lifecycle.begin_load();

			const auto finish = utils::finally([&lifecycle]
			{
				lifecycle.end_load();
			});

			return database_teardown_hook.invoke<std::uintptr_t>();
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			if (is_loot_enabled())
			{
				loot_rarity_scale = game::Dvar_RegisterFloat("cg_lootRarityScale", 1.0f, 0.5f, 3.0f, game::DVAR_FLAG_SAVED);
			}

			accepting_work.store(true, std::memory_order_release);
			scheduler::loop(publish_identity, scheduler::pipeline::main);

			if (is_loot_enabled())
			{
				database_load_hook.create(game::DB_LoadXAssets, database_load_stub);
				database_teardown_hook.create(0xAA610_g, database_teardown_stub);
				scheduler::loop(refresh_loot_catalog, scheduler::pipeline::main);
			}
		}

		void pre_destroy() override
		{
			std::lock_guard publication_lock{publication_mutex};
			accepting_work.store(false, std::memory_order_release);

			demonware::runtime::loot_lifecycle().stop();
			demonware::runtime_context::clear();
		}
	};
}

REGISTER_COMPONENT(demonware_runtime::component)
