#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "component/console/console.hpp"
#include "game/demonware/inventory_cache.hpp"
#include "game/game.hpp"
#include "game/ui_scripting/execution.hpp"

#include <utils/finally.hpp>
#include <utils/hook.hpp>

namespace demonware_loot
{
	namespace
	{
		constexpr std::ptrdiff_t inventory_fetch_task_result_offset = 0x18;
		constexpr std::ptrdiff_t inventory_fetch_result_count_offset = 0x38;

		utils::hook::detour inventory_fetch_success_hook;

		bool refresh_supply_drop_inventory_cache(const std::uint32_t controller_index)
		{
			game::LUI_EnterCriticalSection();
			const auto leave_critical_section = utils::finally(game::LUI_LeaveCriticalSection);

			auto* const state = *game::hks::lui_lua_state;
			if (!state || !state->m_apistack.base || !state->m_apistack.top)
			{
				return false;
			}

			const auto stack_top = state->m_apistack.top;
			const auto restore_stack = utils::finally([state, stack_top]
			{
				state->m_apistack.top = stack_top;
			});

			try
			{
				const auto inventory_utils = ui_scripting::get_globals().get("InventoryUtils");
				if (!inventory_utils.is<ui_scripting::table>())
				{
					return false;
				}

				const auto cache_inventory = inventory_utils.as<ui_scripting::table>().get(
					"CacheSupplyDropInventory");
				if (!cache_inventory.is<ui_scripting::function>())
				{
					return false;
				}

				cache_inventory.as<ui_scripting::function>()(controller_index);
				return true;
			}
			catch (const std::exception& error)
			{
				console::error("[DW] Failed to refresh supply drop inventory cache: %s\n", error.what());
			}

			return false;
		}

		std::optional<std::uint32_t> read_inventory_fetch_result_count(const void* task)
		{
			if (!task)
			{
				return std::nullopt;
			}

			const auto* const task_bytes = static_cast<const std::byte*>(task);
			const auto* const result = *reinterpret_cast<const std::byte* const*>(
				task_bytes + inventory_fetch_task_result_offset);
			if (!result)
			{
				return std::nullopt;
			}

			// 0x27B360 passes task+0x18 to 0xA3DB60, which reads the record count from result+0x38
			return *reinterpret_cast<const std::uint32_t*>(result + inventory_fetch_result_count_offset);
		}

		char handle_inventory_fetch_success_stub(void* task)
		{
			const auto controller_index = *reinterpret_cast<const std::uint32_t*>(0x816A5F4_g);
			const auto items_per_page = *reinterpret_cast<const std::uint32_t*>(0x816A5F8_g);
			const auto result_count = read_inventory_fetch_result_count(task);

			return demonware::inventory_cache::complete_fetch(
				{true, items_per_page, result_count},
				[&]
				{
					return inventory_fetch_success_hook.invoke<char>(task);
				},
				[&]
				{
					refresh_supply_drop_inventory_cache(controller_index);
				});
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated())
			{
				return;
			}

			inventory_fetch_success_hook.create(0x27B360_g, handle_inventory_fetch_success_stub);
		}
	};
}

REGISTER_COMPONENT(demonware_loot::component)
