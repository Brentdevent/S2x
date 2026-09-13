#include <std_include.hpp>
#include "scrollbars.hpp"

#include <CommCtrl.h>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")

namespace console_scrollbars
{
	namespace
	{
		struct scrollbar
		{
			HWND window{};
			bool vertical{};
			int hover{};
			int pressed{};
			int drag_offset{};
			POINT pointer{};
			HWND previous_focus{};
			RECT bounds{};
			SCROLLINFO info{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS};
		};

		struct console_state
		{
			HWND edit{};
			HWND parent{};
			HWND corner{};
			RECT corner_bounds{};
			bool dark{};
			bool updating{};
			bool destroying{};
			scrollbar bars[2]{};
		} state;

		constexpr UINT_PTR repeat_timer = 1;
		constexpr UINT_PTR subclass_id = 1;

		void refresh();

		int maximum(const scrollbar& bar)
		{
			return std::max(bar.info.nMin, bar.info.nMax - static_cast<int>(bar.info.nPage ? bar.info.nPage - 1 : 0));
		}

		SCROLLBARINFO geometry(const scrollbar& bar)
		{
			SCROLLBARINFO info{sizeof(info)};
			GetClientRect(bar.window, &info.rcScrollBar);
			const auto length = bar.vertical ? info.rcScrollBar.bottom : info.rcScrollBar.right;
			const auto thickness = bar.vertical ? info.rcScrollBar.right : info.rcScrollBar.bottom;
			info.dxyLineButton = std::min(thickness, length / 2);
			const auto track = length - 2 * info.dxyLineButton;
			const auto range = std::max(1, bar.info.nMax - bar.info.nMin + 1);
			const auto thumb = std::clamp<LONG>(MulDiv(track, static_cast<int>(bar.info.nPage), range),
				std::min(std::max<LONG>(24, thickness), track), track);
			const auto travel = maximum(bar) - bar.info.nMin;
			info.xyThumbTop = info.dxyLineButton + (travel > 0 ? MulDiv(track - thumb, bar.info.nPos - bar.info.nMin, travel) : 0);
			info.xyThumbBottom = info.xyThumbTop + thumb;
			return info;
		}

		int hit_test(const scrollbar& bar, const POINT point)
		{
			RECT rect{};
			GetClientRect(bar.window, &rect);
			if (!PtInRect(&rect, point) || maximum(bar) <= bar.info.nMin) return 0;
			const auto info = geometry(bar);
			const auto position = bar.vertical ? point.y : point.x;
			const auto length = bar.vertical ? rect.bottom : rect.right;
			if (position < info.dxyLineButton) return 1;
			if (position >= length - info.dxyLineButton) return 5;
			if (position < info.xyThumbTop) return 2;
			if (position >= info.xyThumbBottom) return 4;
			return 3;
		}

		void scroll_action(const scrollbar& bar, const int part)
		{
			const auto action = part == 1 ? SB_LINEUP : part == 5 ? SB_LINEDOWN : part == 2 ? SB_PAGEUP : SB_PAGEDOWN;
			SendMessageA(state.edit, bar.vertical ? WM_VSCROLL : WM_HSCROLL, action, 0);
		}

		void scroll_to(const scrollbar& bar, const int position)
		{
			const auto target = std::clamp(position, bar.info.nMin, maximum(bar));
			if (target == bar.info.nMin || target == maximum(bar))
			{
				SendMessageA(state.edit, bar.vertical ? WM_VSCROLL : WM_HSCROLL, target == bar.info.nMin ? SB_TOP : SB_BOTTOM, 0);
				return;
			}
			if (bar.vertical)
			{
				SendMessageA(state.edit, EM_LINESCROLL, 0, target - bar.info.nPos);
			}
			else
			{
				// EDIT's horizontal scroll range is in pixels; EM_LINESCROLL takes characters.
				const auto dc = GetDC(state.edit);
				const auto font = reinterpret_cast<HFONT>(SendMessageA(state.edit, WM_GETFONT, 0, 0));
				const auto previous = SelectObject(dc, font);
				TEXTMETRICA metrics{};
				GetTextMetricsA(dc, &metrics);
				SelectObject(dc, previous);
				ReleaseDC(state.edit, dc);
				const auto width = std::max<LONG>(1, metrics.tmAveCharWidth);
				const auto delta = target - bar.info.nPos;
				SendMessageA(state.edit, EM_LINESCROLL, (delta + (delta < 0 ? -width / 2 : width / 2)) / width, 0);
			}
		}

		void fill(const HDC dc, const RECT& rect, const COLORREF color)
		{
			SetDCBrushColor(dc, color);
			FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
		}

		void paint(const scrollbar& bar, const HDC dc)
		{
			const auto saved = SaveDC(dc);
			RECT rect{};
			GetClientRect(bar.window, &rect);
			fill(dc, rect, state.dark ? RGB(50, 50, 50) : RGB(255, 255, 255));
			const auto enabled = maximum(bar) > bar.info.nMin;
			const auto info = geometry(bar);
			const auto thickness = bar.vertical ? rect.right : rect.bottom;
			const auto inset = std::max<LONG>(3, thickness / 3);

			if (enabled && info.xyThumbBottom > info.xyThumbTop)
			{
				RECT thumb = bar.vertical
					? RECT{inset, info.xyThumbTop, thickness - inset, info.xyThumbBottom}
					: RECT{info.xyThumbTop, inset, info.xyThumbBottom, thickness - inset};
				const auto active = bar.pressed == 3 || bar.hover == 3 || GetFocus() == bar.window;
				SetDCBrushColor(dc, state.dark ? (active ? RGB(185, 185, 185) : RGB(125, 125, 125))
					: (active ? RGB(100, 100, 100) : RGB(160, 160, 160)));
				const auto brush = SelectObject(dc, GetStockObject(DC_BRUSH));
				const auto pen = SelectObject(dc, GetStockObject(NULL_PEN));
				RoundRect(dc, thumb.left, thumb.top, thumb.right, thumb.bottom, thickness - 2 * inset, thickness - 2 * inset);
				SelectObject(dc, pen);
				SelectObject(dc, brush);
			}

			// Keep visible arrow targets and native SCROLLBAR accessibility/keyboard semantics.
			const auto pen = SelectObject(dc, GetStockObject(DC_PEN));
			for (const auto part : {1, 5})
			{
				const auto active = bar.hover == part || bar.pressed == part;
				const auto color = state.dark ? (enabled ? (active ? RGB(210, 210, 210) : RGB(145, 145, 145)) : RGB(75, 75, 75))
					: (enabled ? RGB(110, 110, 110) : RGB(215, 215, 215));
				SetDCPenColor(dc, color);
				const auto length = bar.vertical ? rect.bottom : rect.right;
				const auto center = part == 1 ? info.dxyLineButton / 2 : length - 1 - info.dxyLineButton / 2;
				const auto radius = std::max<LONG>(2, thickness / 5);
				const auto direction = part == 1 ? -1 : 1;
				POINT points[3] = {{thickness / 2 - radius, center - direction * radius / 2},
					{thickness / 2, center + direction * (radius - radius / 2)}, {thickness / 2 + radius, center - direction * radius / 2}};
				if (!bar.vertical) for (auto& point : points) std::swap(point.x, point.y);
				Polyline(dc, points, 3);
				// GDI omits the final endpoint; include it so both arms have the same length.
				SetPixelV(dc, points[2].x, points[2].y, color);
			}
			SelectObject(dc, pen);
			RestoreDC(dc, saved);
		}

		LRESULT CALLBACK bar_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data)
		{
			auto& bar = *reinterpret_cast<scrollbar*>(data);
			switch (message)
			{
			case WM_NCDESTROY:
				KillTimer(window, repeat_timer);
				RemoveWindowSubclass(window, bar_proc, subclass_id);
				bar.window = nullptr;
				return DefSubclassProc(window, message, wparam, lparam);
			case WM_PAINT:
			{
				PAINTSTRUCT ps{};
				const auto dc = BeginPaint(window, &ps);
				paint(bar, dc);
				EndPaint(window, &ps);
				return 0;
			}
			case WM_PRINT:
			case WM_PRINTCLIENT:
				paint(bar, reinterpret_cast<HDC>(wparam));
				return 0;
			case WM_ERASEBKGND:
				return 1;
			case WM_SETFOCUS:
			case WM_KILLFOCUS:
			case WM_ENABLE:
				InvalidateRect(window, nullptr, FALSE);
				return 0;
			case WM_MOUSEMOVE:
			{
				const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
				bar.pointer = point;
				bar.hover = hit_test(bar, point);
				TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
				TrackMouseEvent(&track);
				if (bar.pressed == 3 && GetCapture() == window)
				{
					const auto info = geometry(bar);
					RECT rect{};
					GetClientRect(window, &rect);
					const auto travel = (bar.vertical ? rect.bottom : rect.right) - 2 * info.dxyLineButton - (info.xyThumbBottom - info.xyThumbTop);
					const auto offset = (bar.vertical ? point.y : point.x) - bar.drag_offset - info.dxyLineButton;
					if (travel > 0) scroll_to(bar, bar.info.nMin + MulDiv(std::clamp<LONG>(offset, 0, travel), maximum(bar) - bar.info.nMin, travel));
				}
				InvalidateRect(window, nullptr, FALSE);
				return 0;
			}
			case WM_MOUSELEAVE:
				bar.hover = 0;
				InvalidateRect(window, nullptr, FALSE);
				return 0;
			case WM_LBUTTONDOWN:
			case WM_LBUTTONDBLCLK:
			{
				const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
				bar.pointer = point;
				bar.pressed = hit_test(bar, point);
				if (!bar.pressed) return 0;
				const auto previous_focus = SetFocus(window);
				if (previous_focus != window) bar.previous_focus = previous_focus;
				SetCapture(window);
				bar.drag_offset = (bar.vertical ? point.y : point.x) - geometry(bar).xyThumbTop;
				if (bar.pressed != 3)
				{
					scroll_action(bar, bar.pressed);
					SetTimer(window, repeat_timer, 350, nullptr);
				}
				InvalidateRect(window, nullptr, FALSE);
				return 0;
			}
			case WM_TIMER:
				if (wparam == repeat_timer && bar.pressed && bar.pressed != 3)
				{
					if (hit_test(bar, bar.pointer) == bar.pressed) scroll_action(bar, bar.pressed);
					SetTimer(window, repeat_timer, 60, nullptr);
					return 0;
				}
				break;
			case WM_LBUTTONUP:
			case WM_CANCELMODE:
				if (GetCapture() == window) ReleaseCapture();
				[[fallthrough]];
			case WM_CAPTURECHANGED:
				bar.pressed = 0;
				KillTimer(window, repeat_timer);
				InvalidateRect(window, nullptr, FALSE);
				return 0;
			case WM_KEYDOWN:
			{
				int action{};
				switch (wparam)
				{
				case VK_TAB: case VK_ESCAPE: case VK_RETURN:
					SetFocus(IsWindow(bar.previous_focus) && bar.previous_focus != window ? bar.previous_focus : GetDlgItem(state.parent, 0x65));
					return 0;
				case VK_UP: case VK_LEFT: action = SB_LINEUP; break;
				case VK_DOWN: case VK_RIGHT: action = SB_LINEDOWN; break;
				case VK_PRIOR: action = SB_PAGEUP; break;
				case VK_NEXT: action = SB_PAGEDOWN; break;
				case VK_HOME: action = SB_TOP; break;
				case VK_END: action = SB_BOTTOM; break;
				default: return DefSubclassProc(window, message, wparam, lparam);
				}
				SendMessageA(state.edit, bar.vertical ? WM_VSCROLL : WM_HSCROLL, action, 0);
				return 0;
			}
			case WM_MOUSEWHEEL:
			case WM_MOUSEHWHEEL:
				return SendMessageA(state.edit, message, wparam, lparam);
			case SBM_SETSCROLLINFO:
			case SBM_SETPOS:
			{
				// Native state is retained for accessibility; painting belongs to this subclass.
				const auto result = message == SBM_SETSCROLLINFO ? DefSubclassProc(window, message, FALSE, lparam)
					: DefSubclassProc(window, message, wparam, FALSE);
				GetScrollInfo(window, SB_CTL, &bar.info);
				InvalidateRect(window, nullptr, FALSE);
				return result;
			}
			}
			return DefSubclassProc(window, message, wparam, lparam);
		}

		void refresh()
		{
			if (!state.edit || state.updating || state.destroying) return;
			state.updating = true;
			for (auto& bar : state.bars)
			{
				SCROLLBARINFO geometry{sizeof(geometry)};
				GetScrollBarInfo(state.edit, bar.vertical ? OBJID_VSCROLL : OBJID_HSCROLL, &geometry);
				MapWindowPoints(nullptr, state.parent, reinterpret_cast<POINT*>(&geometry.rcScrollBar), 2);
				if (!EqualRect(&bar.bounds, &geometry.rcScrollBar))
				{
					bar.bounds = geometry.rcScrollBar;
					SetWindowPos(bar.window, HWND_TOP, bar.bounds.left, bar.bounds.top, bar.bounds.right - bar.bounds.left,
						bar.bounds.bottom - bar.bounds.top, SWP_NOACTIVATE);
				}
				SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
				GetScrollInfo(state.edit, bar.vertical ? SB_VERT : SB_HORZ, &info);
				if (info.nMin != bar.info.nMin || info.nMax != bar.info.nMax || info.nPage != bar.info.nPage || info.nPos != bar.info.nPos)
				{
					SetScrollInfo(bar.window, SB_CTL, &info, FALSE);
				}
			}
			const auto& horizontal = state.bars[0].bounds;
			const auto& vertical = state.bars[1].bounds;
			const RECT corner{vertical.left, horizontal.top, vertical.right, horizontal.bottom};
			if (!EqualRect(&state.corner_bounds, &corner))
			{
				state.corner_bounds = corner;
				SetWindowPos(state.corner, HWND_TOP, corner.left, corner.top, corner.right - corner.left,
					corner.bottom - corner.top, SWP_NOACTIVATE);
			}
			state.updating = false;
		}

		LRESULT CALLBACK parent_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR)
		{
			// Forward notifications from the native accessibility provider as well.
			if ((message == WM_HSCROLL || message == WM_VSCROLL) && lparam &&
				(reinterpret_cast<HWND>(lparam) == state.bars[0].window || reinterpret_cast<HWND>(lparam) == state.bars[1].window))
			{
				SendMessageA(state.edit, message, wparam, 0);
				return 0;
			}
			if (message == WM_DESTROY) state.destroying = true;
			if (message == WM_PARENTNOTIFY && LOWORD(wparam) == WM_DESTROY && reinterpret_cast<HWND>(lparam) == state.corner)
			{
				state.corner = nullptr;
			}
			if (message == WM_COMMAND && reinterpret_cast<HWND>(lparam) == state.edit)
			{
				const auto code = HIWORD(wparam);
				if (code == EN_CHANGE || code == EN_HSCROLL || code == EN_VSCROLL) refresh();
			}
			return DefSubclassProc(window, message, wparam, lparam);
		}

		void destroy_controls()
		{
			state.destroying = true;
			RemoveWindowSubclass(state.parent, parent_proc, subclass_id);
			for (auto& bar : state.bars) if (bar.window) DestroyWindow(bar.window);
			if (state.corner) DestroyWindow(state.corner);
			state = {};
		}

		LRESULT CALLBACK edit_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR)
		{
			if (message == WM_NCDESTROY)
			{
				RemoveWindowSubclass(window, edit_proc, subclass_id);
				destroy_controls();
				return DefSubclassProc(window, message, wparam, lparam);
			}
			const auto result = DefSubclassProc(window, message, wparam, lparam);
			switch (message)
			{
			case WM_SETTEXT: case EM_REPLACESEL: case EM_LINESCROLL: case EM_SCROLL: case EM_SCROLLCARET:
			case WM_VSCROLL: case WM_HSCROLL: case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: case WM_KEYDOWN:
			case WM_SIZE: case WM_SETFONT: case WM_MOUSEMOVE: case WM_LBUTTONUP:
			case WM_PAINT:
				refresh();
			}
			return result;
		}
	}

	void attach(const HWND edit, const bool dark)
	{
		HIGHCONTRASTA contrast{sizeof(contrast)};
		SystemParametersInfoA(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
		if (!edit || state.edit || (contrast.dwFlags & HCF_HIGHCONTRASTON)) return;

		state.edit = edit;
		state.parent = GetParent(edit);
		state.dark = dark;
		const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrA(edit, GWLP_HINSTANCE));
		for (auto i = 0; i < 2; ++i)
		{
			auto& bar = state.bars[i];
			bar.vertical = i == 1;
			bar.window = CreateWindowExA(0, "SCROLLBAR", i ? "Console vertical scroll" : "Console horizontal scroll",
				WS_CHILD | WS_VISIBLE | WS_TABSTOP | (i ? SBS_VERT : SBS_HORZ), 0, 0, 1, 1, state.parent, nullptr, instance, nullptr);
			if (!bar.window || !SetWindowSubclass(bar.window, bar_proc, subclass_id, reinterpret_cast<DWORD_PTR>(&bar)))
			{
				destroy_controls();
				return;
			}
		}
		state.corner = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, state.parent, nullptr, instance, nullptr);
		if (!state.corner || !SetWindowSubclass(state.parent, parent_proc, subclass_id, 0) ||
			!SetWindowSubclass(edit, edit_proc, subclass_id, 0))
		{
			destroy_controls();
			return;
		}
		// Sibling controls cover only the native scrollbar gutters. EDIT still owns text,
		// selection, wheel scrolling, and range calculation; its paints cannot cover the bars.
		SetWindowLongPtrA(edit, GWL_STYLE, GetWindowLongPtrA(edit, GWL_STYLE) | WS_CLIPSIBLINGS);
		refresh();
	}
}
