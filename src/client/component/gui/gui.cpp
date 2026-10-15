#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "component/console/console.hpp"
#include "gui.hpp"

#include <utils/concurrency.hpp>
#include <utils/hook.hpp>
#include <utils/string.hpp>

#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace gui
{
	namespace
	{
		struct notification_t
		{
			std::string title;
			std::string text;
			std::chrono::milliseconds duration{};
			std::chrono::steady_clock::time_point creation_time{};
		};

		struct frame_callback
		{
			std::function<void()> callback;
			bool always;
		};

		struct window_event
		{
			HWND hwnd;
			UINT msg;
			WPARAM wparam;
			LPARAM lparam;
		};

		struct menu_t
		{
			std::string name;
			std::string title;
		};

		utils::concurrency::container<std::vector<frame_callback>> on_frame_callbacks;
		utils::concurrency::container<std::deque<notification_t>> notifications;
		utils::concurrency::container<std::vector<window_event>> event_queue;

		std::vector<menu_t> menus;
		std::unordered_map<std::string, bool> enabled_menus;

		std::atomic_bool toggled{false};
		std::atomic_bool focus_lost{false};

		bool initialized = false;
		ID3D11Device* device{};
		ID3D11DeviceContext* device_context{};

		HWND window{};
		WNDPROC original_wnd_proc{};
		bool window_unicode = false;

		std::string ini_path;

		bool is_supported()
		{
			return game::environment::uses_multiplayer_binary() && !game::environment::is_dedicated();
		}

		LRESULT CALLBACK wnd_proc_stub(const HWND hwnd, const UINT msg, const WPARAM wparam, const LPARAM lparam)
		{
			const auto is_escape = wparam == VK_ESCAPE && (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR);
			if (toggled && !is_escape)
			{
				event_queue.access([&](std::vector<window_event>& queue)
				{
					queue.emplace_back(hwnd, msg, wparam, lparam);
				});
			}

			return window_unicode
				? CallWindowProcW(original_wnd_proc, hwnd, msg, wparam, lparam)
				: CallWindowProcA(original_wnd_proc, hwnd, msg, wparam, lparam);
		}

		void unhook_window()
		{
			if (window && original_wnd_proc && IsWindow(window))
			{
				if (window_unicode)
				{
					SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_wnd_proc));
				}
				else
				{
					SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_wnd_proc));
				}
			}

			window = nullptr;
			original_wnd_proc = nullptr;
		}

		void hook_window(const HWND hwnd)
		{
			unhook_window();

			window = hwnd;
			window_unicode = IsWindowUnicode(hwnd) != FALSE;

			const auto previous = window_unicode
				? SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wnd_proc_stub))
				: SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wnd_proc_stub));

			original_wnd_proc = reinterpret_cast<WNDPROC>(previous);
		}

		HWND get_output_window(IDXGISwapChain* swap_chain)
		{
			DXGI_SWAP_CHAIN_DESC desc{};
			if (FAILED(swap_chain->GetDesc(&desc)))
			{
				return nullptr;
			}

			return desc.OutputWindow;
		}

		bool initialize(IDXGISwapChain* swap_chain)
		{
			device = *game::mp::dx_device;
			device_context = *game::mp::dx_context;

			const auto hwnd = get_output_window(swap_chain);
			if (!device || !device_context || !hwnd)
			{
				return false;
			}

			console::info("[ImGui] Initializing\n");

			ImGui::CreateContext();
			ImGui::StyleColorsDark();

			auto& io = ImGui::GetIO();
			io.IniFilename = ini_path.data();

			ImGui_ImplWin32_Init(hwnd);
			ImGui_ImplDX11_Init(device, device_context);
			hook_window(hwnd);

			initialized = true;
			return true;
		}

		void update_targets(IDXGISwapChain* swap_chain)
		{
			const auto current_device = *game::mp::dx_device;
			const auto current_context = *game::mp::dx_context;

			if (current_device && current_context && (current_device != device || current_context != device_context))
			{
				ImGui_ImplDX11_Shutdown();
				device = current_device;
				device_context = current_context;
				ImGui_ImplDX11_Init(device, device_context);
			}

			const auto hwnd = get_output_window(swap_chain);
			if (hwnd && hwnd != window)
			{
				ImGui_ImplWin32_Shutdown();
				ImGui_ImplWin32_Init(hwnd);
				hook_window(hwnd);
			}
		}

		void run_event_queue()
		{
			event_queue.access([](std::vector<window_event>& queue)
			{
				for (const auto& event : queue)
				{
					ImGui_ImplWin32_WndProcHandler(event.hwnd, event.msg, event.wparam, event.lparam);
				}

				queue.clear();
			});
		}

		std::string truncate(const std::string& text, const size_t length, const std::string& end)
		{
			return text.size() <= length
				? text
				: text.substr(0, length - end.size()) + end;
		}

		void show_notifications()
		{
			static constexpr auto window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoInputs;

			notifications.access([](std::deque<notification_t>& notifications_)
			{
				auto index = 0;
				for (auto i = notifications_.begin(); i != notifications_.end();)
				{
					if (std::chrono::steady_clock::now() - i->creation_time >= i->duration)
					{
						i = notifications_.erase(i);
						continue;
					}

					const auto title = truncate(i->title, 34, "...");
					const auto text = truncate(i->text, 34, "...");

					ImGui::SetNextWindowPos(ImVec2(10.f, 30.f + static_cast<float>(index) * 60.f));
					ImGui::SetNextWindowSize(ImVec2(250.f, 0.f));
					ImGui::SetNextWindowBgAlpha(0.6f);
					ImGui::Begin(utils::string::va("Notification #%i", index), nullptr, window_flags);
					ImGui::TextUnformatted(title.data());
					ImGui::TextUnformatted(text.data());
					ImGui::End();

					++i;
					++index;
				}
			});
		}

		void draw_main_menu_bar()
		{
			if (ImGui::BeginMainMenuBar())
			{
				if (ImGui::BeginMenu("Windows"))
				{
					for (const auto& menu : menus)
					{
						ImGui::Checkbox(menu.title.data(), &enabled_menus[menu.name]);
					}

					ImGui::EndMenu();
				}

				ImGui::TextDisabled("F11 or Escape to close");
				ImGui::EndMainMenuBar();
			}
		}

		void run_frame_callbacks()
		{
			on_frame_callbacks.access([](const std::vector<frame_callback>& callbacks)
			{
				for (const auto& callback : callbacks)
				{
					if (callback.always || toggled)
					{
						callback.callback();
					}
				}
			});
		}

		DXGI_FORMAT get_render_target_format(const DXGI_FORMAT format)
		{
			switch (format)
			{
			case DXGI_FORMAT_R8G8B8A8_TYPELESS:
				return DXGI_FORMAT_R8G8B8A8_UNORM;
			case DXGI_FORMAT_B8G8R8A8_TYPELESS:
				return DXGI_FORMAT_B8G8R8A8_UNORM;
			case DXGI_FORMAT_R10G10B10A2_TYPELESS:
				return DXGI_FORMAT_R10G10B10A2_UNORM;
			case DXGI_FORMAT_R16G16B16A16_TYPELESS:
				return DXGI_FORMAT_R16G16B16A16_FLOAT;
			default:
				return format;
			}
		}

		void draw_to_back_buffer(IDXGISwapChain* swap_chain)
		{
			CComPtr<ID3D11Texture2D> back_buffer;
			if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))))
			{
				return;
			}

			D3D11_TEXTURE2D_DESC texture_desc{};
			back_buffer->GetDesc(&texture_desc);

			D3D11_RENDER_TARGET_VIEW_DESC view_desc{};
			view_desc.Format = get_render_target_format(texture_desc.Format);
			view_desc.ViewDimension = texture_desc.SampleDesc.Count > 1
				? D3D11_RTV_DIMENSION_TEXTURE2DMS
				: D3D11_RTV_DIMENSION_TEXTURE2D;

			CComPtr<ID3D11RenderTargetView> render_target;
			if (FAILED(device->CreateRenderTargetView(back_buffer, &view_desc, &render_target)))
			{
				return;
			}

			CComPtr<ID3D11RenderTargetView> previous_target;
			CComPtr<ID3D11DepthStencilView> previous_depth;
			device_context->OMGetRenderTargets(1, &previous_target, &previous_depth);

			device_context->OMSetRenderTargets(1, &render_target.p, nullptr);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			device_context->OMSetRenderTargets(1, &previous_target.p, previous_depth);
		}

		bool has_visible_content()
		{
			if (toggled)
			{
				return true;
			}

			const auto has_notifications = notifications.access<bool>([](const std::deque<notification_t>& list)
			{
				return !list.empty();
			});

			if (has_notifications)
			{
				return true;
			}

			return on_frame_callbacks.access<bool>([](const std::vector<frame_callback>& callbacks)
			{
				return std::ranges::any_of(callbacks, [](const frame_callback& callback)
				{
					return callback.always;
				});
			});
		}

		void render(IDXGISwapChain* swap_chain)
		{
			if (!swap_chain)
			{
				return;
			}

			if (!initialized)
			{
				if (!initialize(swap_chain))
				{
					return;
				}
			}
			else
			{
				update_targets(swap_chain);
			}

			if (!has_visible_content())
			{
				return;
			}

			auto& io = ImGui::GetIO();
			io.MouseDrawCursor = toggled;

			if (focus_lost.exchange(false))
			{
				io.AddFocusEvent(false);
			}

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			run_event_queue();
			ImGui::NewFrame();

			show_notifications();
			run_frame_callbacks();

			ImGui::Render();
			draw_to_back_buffer(swap_chain);
		}

		HRESULT present_stub(IDXGISwapChain* swap_chain, const UINT sync_interval, const UINT flags)
		{
			render(swap_chain);
			return swap_chain->Present(sync_interval, flags);
		}

		void mouse_delta_stub(const int delta_x, const int delta_y)
		{
			if (toggled)
			{
				return;
			}

			utils::hook::invoke<void>(0x7888C0_g, delta_x, delta_y);
		}

		void toggle()
		{
			const auto was_open = toggled.exchange(!toggled);
			if (was_open)
			{
				focus_lost = true;
				event_queue.access([](std::vector<window_event>& queue)
				{
					queue.clear();
				});
			}
		}
	}

	bool is_open()
	{
		return toggled;
	}

	bool gui_key_event(const int /*local_client_num*/, const int key, const int down)
	{
		if (!is_supported())
		{
			return true;
		}

		if (key == game::K_F11 && down)
		{
			toggle();
			return false;
		}

		if (key == game::K_ESCAPE && down && toggled)
		{
			toggle();
			return false;
		}

		return !toggled || !down;
	}

	bool gui_char_event(const int /*local_client_num*/, const int /*key*/)
	{
		return !toggled;
	}

	void on_frame(const std::function<void()>& callback, const bool always)
	{
		on_frame_callbacks.access([&](std::vector<frame_callback>& callbacks)
		{
			callbacks.emplace_back(callback, always);
		});
	}

	void notification(const std::string& title, const std::string& text, const std::chrono::milliseconds duration)
	{
		notification_t notification{};
		notification.title = title;
		notification.text = text;
		notification.duration = duration;
		notification.creation_time = std::chrono::steady_clock::now();

		notifications.access([&](std::deque<notification_t>& notifications_)
		{
			notifications_.push_front(std::move(notification));
		});
	}

	void copy_to_clipboard(const std::string& text)
	{
		ImGui::SetClipboardText(text.data());
		notification("Text copied to clipboard", utils::string::va("\"%s\"", text.data()));
	}

	void register_menu(const std::string& name, const std::string& title,
		const std::function<void()>& callback, const bool always)
	{
		menus.emplace_back(name, title);
		auto* enabled = &enabled_menus[name];

		on_frame([enabled, callback]
		{
			if (*enabled)
			{
				callback();
			}
		}, always);
	}

	bool* get_menu_toggle(const std::string& name)
	{
		return &enabled_menus[name];
	}

	bool contains_lower(const char* text, const std::string& filter)
	{
		if (filter.empty())
		{
			return true;
		}

		if (!text)
		{
			return false;
		}

		const std::string_view haystack{text};
		const auto it = std::ranges::search(haystack, filter, [](const char a, const char b)
		{
			return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		});

		return !it.empty();
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			if (!is_supported())
			{
				return;
			}

			ini_path = (game::get_appdata_path() / "imgui.ini").string();

			// replaces the 6 byte guarded IDXGISwapChain::Present call
			utils::hook::nop(0x8E8658_g, 6);
			utils::hook::call(0x8E8658_g, present_stub);

			// mouse delta accumulator call in the raw input poll (0x7B4900)
			utils::hook::call(0x7B4953_g, mouse_delta_stub);

			on_frame([]
			{
				draw_main_menu_bar();
			});
		}
	};
}

REGISTER_COMPONENT(gui::component)
