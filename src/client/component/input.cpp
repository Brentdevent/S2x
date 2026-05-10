#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "game_console.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace input
{
	namespace
	{
		utils::hook::detour cl_char_event_hook;
		utils::hook::detour cl_key_event_hook;

		void cl_char_event_stub(const int local_client_num, const int key)
		{
			if (!game_console::console_char_event(local_client_num, key))
			{
				return;
			}

			cl_char_event_hook.invoke<void>(local_client_num, key);
		}

		void cl_key_event_stub(const int local_client_num, const int key, const signed int down, const int arg4)
		{
			if (!game_console::console_key_event(local_client_num, key, down))
			{
				return;
			}

			cl_key_event_hook.invoke<void>(local_client_num, key, down, arg4);
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			cl_char_event_hook.create(game::select(0x45D960, 0x2656F0), cl_char_event_stub);
			cl_key_event_hook.create(game::select(0x45DBC0, 0x265970), cl_key_event_stub);
		}
	};
}

REGISTER_COMPONENT(input::component)