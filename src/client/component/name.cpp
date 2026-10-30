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
		constexpr std::size_t max_player_name_length = 35;

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

		std::size_t copy_player_name(char* buffer, const std::size_t buffer_size,
			const std::string_view player_name)
		{
			if (!buffer_size)
			{
				return 0;
			}

			auto length = std::min(player_name.size(), buffer_size - 1);

			// If the cut falls inside a UTF-8 character, omit that entire character.
			while (length && length < player_name.size()
				&& (static_cast<unsigned char>(player_name[length]) & 0xC0) == 0x80)
			{
				--length;
			}

			std::memcpy(buffer, player_name.data(), length);
			buffer[length] = '\0';

			return length;
		}

		const char* get_local_player_name(const int controller_index)
		{
			const auto* player_name = get_player_name();
			if (controller_index != 1)
			{
				return player_name;
			}

			constexpr char guest_suffix[] = "(2)";
			thread_local char guest_name[max_player_name_length + 1];

			auto prefix_length = copy_player_name(guest_name,
				sizeof(guest_name) - (sizeof(guest_suffix) - 1), player_name);

			std::memcpy(guest_name + prefix_length, guest_suffix, sizeof(guest_suffix));

			// A full-length primary name may already end in the guest suffix.
			if (std::strcmp(guest_name, player_name) == 0)
			{
				prefix_length = copy_player_name(guest_name, prefix_length, player_name);
				std::memcpy(guest_name + prefix_length, guest_suffix, sizeof(guest_suffix));
			}

			return guest_name;
		}

		char* cl_get_username_for_local_client_stub(const int controller_index)
		{
			return const_cast<char*>(get_local_player_name(controller_index));
		}

		bool live_get_local_client_name_stub(const int controller_index, char* buffer,
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

			copy_player_name(buffer, buffer_size, get_local_player_name(controller_index));
			return true;
		}
	}

	const char* get_player_name()
	{
		const auto* player_name = name_dvar && name_dvar->current.string
			? name_dvar->current.string : get_default_player_name();

		// Keep every primary-name path within the stock name buffer capacity.
		thread_local char bounded_name[max_player_name_length + 1];
		copy_player_name(bounded_name, sizeof(bounded_name), player_name);

		return bounded_name;
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
