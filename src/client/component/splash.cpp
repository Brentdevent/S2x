#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "resource.hpp"

#include <utils/nt.hpp>
#include <utils/hook.hpp>

namespace splash
{
	namespace
	{
		utils::hook::detour load_image_a_hook;

		constexpr auto GAME_SPLASH_ID = 100;

		HINSTANCE get_current_module()
		{
			return utils::nt::library::get_by_address(get_current_module);
		}

		bool is_game_splash_resource(HINSTANCE hInst, LPCSTR name, UINT type)
		{
			if (type != IMAGE_BITMAP)
			{
				return false;
			}

			if (!IS_INTRESOURCE(name))
			{
				return false;
			}

			const auto resource_id = LOWORD(reinterpret_cast<ULONG_PTR>(name));
			if (resource_id != GAME_SPLASH_ID)
			{
				return false;
			}

			return true;
		}

		HANDLE WINAPI load_image_a_stub(HINSTANCE hInst, LPCSTR name, UINT type, int _cx, int cy, UINT fuLoad)
		{
			if (is_game_splash_resource(hInst, name, type))
			{
				return load_image_a_hook.invoke<HANDLE>(get_current_module(), MAKEINTRESOURCEA(IMAGE_SPLASH), type, _cx, cy, fuLoad);
			}

			return load_image_a_hook.invoke<HANDLE>(hInst, name, type, _cx, cy, fuLoad);
		}
	}

	class component final : public generic_component
	{
	public:
		component()
		{
			load_image_a_hook.create(LoadImageA, load_image_a_stub);
		}
	};
}

REGISTER_COMPONENT(splash::component)
