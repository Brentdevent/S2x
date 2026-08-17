#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "name.hpp"

#include "game/game.hpp"

#include "steam_proxy.hpp"

#include <utils/hook.hpp>

namespace name
{
	namespace
	{
		game::dvar_t* name_dvar{};
		
		utils::hook::detour com_init_dvars_hook;
		utils::hook::detour cl_get_username_for_local_client_hook;
		utils::hook::detour live_get_local_client_name_hook;

		const char* get_default_player_name()
		{
			const auto* default_name = steam_proxy::get_player_name();
			return default_name && *default_name ? default_name : "Unknown Soldier";
		}

		void register_name_dvar()
		{
			name_dvar = game::Dvar_RegisterString("name", get_default_player_name(),
				static_cast<game::DvarFlags>(game::DVAR_FLAG_SAVED | game::DVAR_FLAG_USERINFO));
		}

		void com_init_dvars_stub()
		{
			register_name_dvar();
			com_init_dvars_hook.invoke<void>();
		}

		char* cl_get_username_for_local_client_stub(const int /*controller_index*/)
		{
			return const_cast<char*>(get_player_name());
		}

		bool live_get_local_client_name_stub(const int /*controller_index*/, char* buffer,
			const std::size_t buffer_size)
		{
			if (!buffer || buffer_size <= 1)
			{
				if (buffer && buffer_size)
				{
					buffer[0] = '\0';
				}

				return false;
			}

			strncpy_s(buffer, buffer_size, get_player_name(), _TRUNCATE);
			return true;
		}
	}

	const char* get_player_name()
	{
		if (name_dvar && name_dvar->current.string)
		{
			return name_dvar->current.string;
		}

		return get_default_player_name();
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			com_init_dvars_hook.create(game::Com_InitDvars, com_init_dvars_stub);

			cl_get_username_for_local_client_hook.create(game::CL_GetUsernameForLocalClient,
				cl_get_username_for_local_client_stub);
			live_get_local_client_name_hook.create(game::Live_GetLocalClientName,
				live_get_local_client_name_stub);
		}

		component_priority priority() const override
		{
			return component_priority::name;
		}
	};
}

REGISTER_COMPONENT(name::component)
