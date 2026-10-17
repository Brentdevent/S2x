#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>

namespace game_window
{
	namespace
	{
		BOOL WINAPI set_window_placement_stub(const HWND window, const WINDOWPLACEMENT* placement)
		{
			if (!placement || placement->length != sizeof(WINDOWPLACEMENT))
			{
				return SetWindowPlacement(window, placement);
			}

			const auto style = GetWindowLongPtrA(window, GWL_STYLE);
			const auto ex_style = GetWindowLongPtrA(window, GWL_EXSTYLE);

			if (!(style & WS_POPUP) || (style & (WS_CHILD | WS_CAPTION | WS_THICKFRAME))
				|| (ex_style & WS_EX_TOOLWINDOW))
			{
				return SetWindowPlacement(window, placement);
			}

			MONITORINFO monitor{sizeof(monitor)};
			const auto handle = MonitorFromRect(&placement->rcNormalPosition, MONITOR_DEFAULTTONULL);

			if (!handle || !GetMonitorInfoW(handle, &monitor)
				|| !EqualRect(&placement->rcNormalPosition, &monitor.rcMonitor))
			{
				return SetWindowPlacement(window, placement);
			}

			// The renderer passes the full monitor rectangle in screen coordinates.
			// SetWindowPlacement expects workspace coordinates for non-tool windows.
			auto corrected = *placement;
			OffsetRect(&corrected.rcNormalPosition, monitor.rcMonitor.left - monitor.rcWork.left,
				monitor.rcMonitor.top - monitor.rcWork.top);

			return SetWindowPlacement(window, &corrected);
		}
	}

	class component final : public generic_component
	{
	public:
		void post_load() override
		{
			if (game::environment::is_dedicated())
			{
				return;
			}

			const auto hook = utils::hook::iat(utils::nt::library{}, "user32.dll", "SetWindowPlacement",
				set_window_placement_stub);

			if (!hook)
			{
				throw std::runtime_error("Unable to hook game SetWindowPlacement import");
			}
		}
	};
}

REGISTER_COMPONENT(game_window::component)
