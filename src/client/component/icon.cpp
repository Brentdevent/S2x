#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "resource.hpp"

#include <utils/nt.hpp>
#include <utils/hook.hpp>

namespace icon
{
	namespace
	{
		utils::hook::detour load_icon_a_hook;

		constexpr auto GAME_ICON_ID = 2;

		HINSTANCE get_current_module()
		{
			return utils::nt::library::get_by_address(get_current_module);
		}

		bool is_game_icon_resource(HINSTANCE module, LPCSTR icon_name)
		{
			if (!IS_INTRESOURCE(icon_name))
			{
				return false;
			}

			const auto resource_id = LOWORD(reinterpret_cast<ULONG_PTR>(icon_name));
			if (resource_id != GAME_ICON_ID)
			{
				return false;
			}

			return true;
		}

		HICON WINAPI load_icon_a_stub(HINSTANCE module, LPCSTR icon_name)
		{
			if (is_game_icon_resource(module, icon_name))
			{
				return load_icon_a_hook.invoke<HICON>(get_current_module(), MAKEINTRESOURCEA(ID_ICON));
			}

			return load_icon_a_hook.invoke<HICON>(module, icon_name);
		}
	}

	class component final : public generic_component
	{
	public:
		component()
		{
			load_icon_a_hook.create(LoadIconA, load_icon_a_stub);
		}
	};
}

REGISTER_COMPONENT(icon::component)
