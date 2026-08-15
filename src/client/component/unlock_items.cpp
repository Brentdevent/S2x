#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>

namespace unlock_items
{
	namespace
	{
		const game::dvar_t* cg_unlock_all_items{};

		utils::hook::detour live_storage_is_item_unlocked_from_table_hook;
		utils::hook::detour live_storage_is_item_unlocked_from_table_local_client_hook;

		bool is_normal_unlock(const game::StringTable* unlock_table, const int row)
		{
			if (!unlock_table || !unlock_table->values || row < 0 || row >= unlock_table->rowCount ||
				unlock_table->columnCount <= 1)
			{
				return false;
			}

			const auto* unlock_type = unlock_table->values[row * unlock_table->columnCount + 1].string;
			return unlock_type && std::strcmp(unlock_type, "loot") != 0;
		}

		int live_storage_is_item_unlocked_from_table_stub(const unsigned int item_id, const int controller_index,
			void* stats_source, void* stats_buffer, game::StringTable* unlock_table, const int row, void* out_param)
		{
			if (cg_unlock_all_items && cg_unlock_all_items->current.enabled && is_normal_unlock(unlock_table, row))
			{
				return 0;
			}

			return live_storage_is_item_unlocked_from_table_hook.invoke<int>(item_id, controller_index, stats_source,
				stats_buffer, unlock_table, row, out_param);
		}

		int live_storage_is_item_unlocked_from_table_local_client_stub(const unsigned int local_client_num,
			game::StringTable* unlock_table, const int row, const unsigned int item_id)
		{
			if (cg_unlock_all_items && cg_unlock_all_items->current.enabled && is_normal_unlock(unlock_table, row))
			{
				return 0;
			}

			return live_storage_is_item_unlocked_from_table_local_client_hook.invoke<int>(local_client_num,
				unlock_table, row, item_id);
		}

		int item_unlocked()
		{
			return 0;
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated())
			{
				utils::hook::jump(0xD0B10_g, item_unlocked);
				utils::hook::jump(0xD1050_g, item_unlocked);
				return;
			}

			cg_unlock_all_items = game::Dvar_RegisterBool("cg_unlockall_items", false, game::DVAR_FLAG_SAVED);

			live_storage_is_item_unlocked_from_table_hook.create(0xD0B10_g,
				live_storage_is_item_unlocked_from_table_stub);
			live_storage_is_item_unlocked_from_table_local_client_hook.create(0xD1050_g,
				live_storage_is_item_unlocked_from_table_local_client_stub);
		}
	};
}

REGISTER_COMPONENT(unlock_items::component)
