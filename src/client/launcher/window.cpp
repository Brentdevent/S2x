#include <std_include.hpp>
#include "window.hpp"
#include "resource.hpp"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

window::window(const std::string& title, const int width, const int height,
	std::function<std::optional<LRESULT>(window*, UINT, WPARAM, LPARAM)> callback, const long flags) :
	callback_(std::move(callback))
{
	this->classname_ = "s2x-launcher-" + std::to_string(reinterpret_cast<uintptr_t>(this));
	this->wc_.cbSize = sizeof(this->wc_);
	this->wc_.style = CS_HREDRAW | CS_VREDRAW;
	this->wc_.lpfnWndProc = static_processor;
	this->wc_.hInstance = GetModuleHandleW(nullptr);
	this->wc_.hCursor = LoadCursor(nullptr, IDC_ARROW);
	this->wc_.hIcon = LoadIcon(this->wc_.hInstance, MAKEINTRESOURCE(ID_ICON));
	this->wc_.hIconSm = this->wc_.hIcon;
	this->wc_.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
	this->wc_.lpszClassName = this->classname_.c_str();

	if (!RegisterClassExA(&this->wc_))
	{
		throw std::runtime_error("Unable to register launcher window");
	}

	const auto x = GetSystemMetrics(SM_CXSCREEN) / 2;
	const auto y = GetSystemMetrics(SM_CYSCREEN) / 2;
	this->handle_ = CreateWindowExA(
		0, this->wc_.lpszClassName, title.c_str(), flags, x, y, 0, 0, nullptr, nullptr, this->wc_.hInstance, this);

	if (!this->handle_)
	{
		UnregisterClassA(this->wc_.lpszClassName, this->wc_.hInstance);
		throw std::runtime_error("Unable to create launcher window");
	}

	const auto dpi = this->get_dpi();
	// Match the original launcher: width and height include the title bar and borders.
	const auto outer_width = MulDiv(width, dpi, 96);
	const auto outer_height = MulDiv(height, dpi, 96);

	MONITORINFO monitor{sizeof(monitor)};
	GetMonitorInfoW(MonitorFromWindow(this->handle_, MONITOR_DEFAULTTONEAREST), &monitor);
	SetWindowPos(this->handle_, nullptr,
		monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - outer_width) / 2,
		monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - outer_height) / 2, outer_width, outer_height,
		SWP_NOZORDER | SWP_NOACTIVATE);

	const BOOL dark = TRUE;
	DwmSetWindowAttribute(this->handle_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
	// Show only after the first frame is ready.
}

window::~window()
{
	this->close();
	UnregisterClassA(this->wc_.lpszClassName, this->wc_.hInstance);
}

void window::close()
{
	if (this->handle_)
	{
		DestroyWindow(this->handle_);
	}
}

void window::show()
{
	ShowWindow(this->handle_, SW_SHOW);
	SetForegroundWindow(this->handle_);
}

uint32_t window::get_dpi() const
{
	const auto get_dpi =
		reinterpret_cast<UINT(WINAPI*)(HWND)>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
	const auto dpi = get_dpi ? get_dpi(this->handle_) : 96;

	return dpi ? dpi : 96;
}

LRESULT window::processor(const UINT message, const WPARAM w_param, const LPARAM l_param)
{
	if (message == WM_DPICHANGED && l_param)
	{
		const auto& bounds = *reinterpret_cast<const RECT*>(l_param);
		SetWindowPos(this->handle_, nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
			bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
	}

	if (this->callback_)
	{
		const auto result = this->callback_(this, message, w_param, l_param);

		if (result)
		{
			return *result;
		}
	}

	return DefWindowProcA(this->handle_, message, w_param, l_param);
}

LRESULT CALLBACK window::static_processor(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param)
{
	if (message == WM_NCCREATE)
	{
		const auto data = reinterpret_cast<const CREATESTRUCTA*>(l_param);
		auto* self = static_cast<window*>(data->lpCreateParams);
		self->handle_ = hwnd;
		SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	}

	auto* self = reinterpret_cast<window*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

	if (!self)
	{
		return DefWindowProcA(hwnd, message, w_param, l_param);
	}

	const auto result = self->processor(message, w_param, l_param);

	if (message == WM_NCDESTROY)
	{
		SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
		self->handle_ = nullptr;
	}

	return result;
}

window::operator HWND() const
{
	return this->handle_;
}
