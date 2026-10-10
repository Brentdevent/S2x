#include <std_include.hpp>
#include "rml_window.hpp"
#include "resources.hpp"
#include "window.hpp"

#include <RmlUi/Core.h>
#include <RmlUi_Platform_Win32.h>
#include <RmlUi_Renderer_DX11.h>
#include <wincodec.h>
#include <shlwapi.h>

namespace
{
	void check_result(const HRESULT result, const char* operation)
	{
		if (FAILED(result))
		{
			char code[16]{};
			snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(result));
			throw std::runtime_error(std::string(operation) + " (" + code + ")");
		}
	}

	class com_scope final
	{
	public:
		com_scope()
		{
			this->result_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

			if (this->result_ != RPC_E_CHANGED_MODE)
			{
				check_result(this->result_, "Unable to initialize image decoding");
			}
		}

		~com_scope()
		{
			if (SUCCEEDED(this->result_))
			{
				CoUninitialize();
			}
		}

	private:
		HRESULT result_{};
	};

	class system_interface final : public SystemInterface_Win32
	{
	public:
		std::string error;

		bool LogMessage(const Rml::Log::Type type, const Rml::String& message) override
		{
			OutputDebugStringA(("RmlUi: " + message + "\n").c_str());

			if (type <= Rml::Log::LT_WARNING)
			{
				this->error += message + "\n";
			}

			// Do not open RmlUi's default assertion dialog inside the launcher.
			return true;
		}
	};

	class render_interface final : public RenderInterface_DX11
	{
	public:
		explicit render_interface(ID3D11Device* device) :
			RenderInterface_DX11(device)
		{
			check_result(
				this->imaging_.CoCreateInstance(CLSID_WICImagingFactory), "Unable to initialize launcher images");
		}

		Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override
		{
			const auto bytes = launcher_resources::load(source);

			if (bytes.empty())
			{
				throw std::runtime_error("Missing launcher image: " + source);
			}

			CComPtr<IStream> stream;
			stream.Attach(
				SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()), static_cast<UINT>(bytes.size())));

			if (!stream)
			{
				throw std::runtime_error("Unable to read launcher image: " + source);
			}

			CComPtr<IWICBitmapDecoder> decoder;
			CComPtr<IWICBitmapFrameDecode> frame;
			CComPtr<IWICFormatConverter> converter;
			check_result(
				this->imaging_->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder),
				"Unable to decode launcher image");
			check_result(decoder->GetFrame(0, &frame), "Unable to read launcher image frame");
			check_result(this->imaging_->CreateFormatConverter(&converter), "Unable to convert launcher image");
			check_result(converter->Initialize(frame, GUID_WICPixelFormat32bppPRGBA, WICBitmapDitherTypeNone, nullptr,
							 0, WICBitmapPaletteTypeCustom),
				"Unable to convert launcher pixels");

			UINT width{}, height{};
			check_result(converter->GetSize(&width, &height), "Unable to read launcher image dimensions");

			if (!width || !height || width > 8192 || height > 8192)
			{
				throw std::runtime_error("Invalid launcher image dimensions");
			}

			std::vector<Rml::byte> pixels(static_cast<size_t>(width) * height * 4);
			check_result(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()),
				"Unable to read launcher pixels");
			dimensions = {static_cast<int>(width), static_cast<int>(height)};

			return this->GenerateTexture(pixels, dimensions);
		}

	private:
		CComPtr<IWICImagingFactory> imaging_;
	};
}

class rml_window::implementation final : public Rml::EventListener
{
public:
	implementation(const std::string& title, const int width, const int height)
	{
		try
		{
			this->window_ = std::make_unique<window>(title, width, height,
				[this](window*, UINT message, WPARAM w_param, LPARAM l_param)
				{
					return this->process_message(message, w_param, l_param);
				});
			this->system_.SetWindow(*this->window_);
			this->create_device();
			Rml::SetSystemInterface(&this->system_);
			Rml::SetFileInterface(&this->files_);
			this->renderer_ = std::make_unique<render_interface>(this->device_);
			Rml::SetRenderInterface(this->renderer_.get());
			Rml::SetTextInputHandler(&this->text_input_);
			this->initialized_ = Rml::Initialise();

			if (!this->initialized_)
			{
				throw std::runtime_error("Unable to initialize the launcher interface");
			}

			if (!Rml::LoadFontFace(
					"title.woff", "launcher-title", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal)
				|| !Rml::LoadFontFace(
					"body.ttf", "launcher-body", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, true))
			{
				throw std::runtime_error("Unable to load launcher fonts:\n" + this->system_.error);
			}

			this->context_ = Rml::CreateContext("launcher", this->dimensions_);

			if (!this->context_)
			{
				throw std::runtime_error("Unable to create the launcher interface");
			}

			this->context_->SetDensityIndependentPixelRatio(this->window_->get_dpi() / 96.f);
		}
		catch (...)
		{
			this->shutdown();
			throw;
		}
	}

	~implementation() override
	{
		this->shutdown();
	}

	void shutdown()
	{
		// RmlUi releases fonts, textures and listeners while the interfaces and device still exist.
		if (this->initialized_)
		{
			Rml::Shutdown();
		}

		this->context_ = nullptr;
		this->initialized_ = false;
		Rml::SetTextInputHandler(nullptr);
		Rml::SetRenderInterface(nullptr);
		Rml::SetFileInterface(nullptr);
		Rml::SetSystemInterface(nullptr);
		this->renderer_.reset();
		this->system_.SetWindow(nullptr);
		this->window_.reset();
	}

	void create_device()
	{
		RECT area{};
		GetClientRect(*this->window_, &area);
		this->dimensions_ = {area.right, area.bottom};

		DXGI_SWAP_CHAIN_DESC description{};
		description.BufferCount = 1;
		description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		description.OutputWindow = *this->window_;
		description.SampleDesc.Count = 1;
		description.Windowed = TRUE;
		description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
		auto result = E_FAIL;
		// Software rendering also allows the launcher to work without an accelerated desktop.
		for (const auto driver : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP})
		{
			result = D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, levels, ARRAYSIZE(levels),
				D3D11_SDK_VERSION, &description, &this->swap_chain_, &this->device_, nullptr, &this->device_context_);

			if (SUCCEEDED(result))
			{
				CComPtr<ID3D11Device1> device1;
				result = this->device_.QueryInterface(&device1);

				for (const auto format : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT_S8X24_UINT})
				{
					if (FAILED(result))
					{
						break;
					}

					UINT quality{};
					result = this->device_->CheckMultisampleQualityLevels(format, NUM_MSAA_SAMPLES, &quality);

					if (SUCCEEDED(result) && !quality)
					{
						result = DXGI_ERROR_UNSUPPORTED;
					}
				}
			}

			if (SUCCEEDED(result))
			{
				break;
			}

			this->swap_chain_.Release();
			this->device_.Release();
			this->device_context_.Release();
		}

		check_result(result, "Unable to initialize launcher graphics");
	}

	Rml::ElementDocument* load_document(const std::string& name)
	{
		auto* document = this->context_->LoadDocument(name);

		if (!document)
		{
			throw std::runtime_error("Unable to load the launcher layout");
		}

		document->AddEventListener(Rml::EventId::Click, this);
		document->AddEventListener(Rml::EventId::Change, this);
		document->AddEventListener(Rml::EventId::Keydown, this);
		document->AddEventListener(Rml::EventId::Focus, this, true);
		document->Show();
		this->context_->Update();
		this->check_errors();

		return document;
	}

	void ProcessEvent(Rml::Event& event) override
	{
		// Callbacks may request closure, but destruction waits until event dispatch has unwound.
		if (this->event_handler_)
		{
			this->event_handler_(event);
		}
	}

	std::optional<LRESULT> process_message(const UINT message, const WPARAM w_param, const LPARAM l_param)
	{
		try
		{
			if (message == WM_CLOSE)
			{
				this->running_ = false;

				return 0;
			}

			if (message == WM_ERASEBKGND)
			{
				return 1;
			}

			if (message == WM_SIZE)
			{
				this->dimensions_ = {LOWORD(l_param), HIWORD(l_param)};
				this->resize_ = true;
			}

			if (message == WM_DPICHANGED && this->context_)
			{
				this->context_->SetDensityIndependentPixelRatio(this->window_->get_dpi() / 96.f);
			}

			if (this->context_
				&& !RmlWin32::WindowProcedure(
					this->context_, this->text_input_, *this->window_, message, w_param, l_param))
			{
				return 0;
			}
		}
		catch (...)
		{
			// Never unwind a C++ exception through the Windows window procedure.
			this->error_ = std::current_exception();
			this->running_ = false;
		}

		return {};
	}

	void check_errors() const
	{
		if (!this->system_.error.empty())
		{
			throw std::runtime_error("Unable to render the launcher:\n" + this->system_.error);
		}
	}

	void render()
	{
		if (this->resize_)
		{
			this->device_context_->ClearState();
			this->target_.Release();
			check_result(
				this->swap_chain_->ResizeBuffers(0, this->dimensions_.x, this->dimensions_.y, DXGI_FORMAT_UNKNOWN, 0),
				"Unable to resize launcher graphics");
			CComPtr<ID3D11Texture2D> buffer;
			check_result(this->swap_chain_->GetBuffer(0, IID_PPV_ARGS(&buffer)), "Unable to get launcher surface");
			check_result(this->device_->CreateRenderTargetView(buffer, nullptr, &this->target_),
				"Unable to create launcher surface");
			this->renderer_->SetViewport(this->dimensions_.x, this->dimensions_.y);
			this->context_->SetDimensions(this->dimensions_);
			this->resize_ = false;
		}

		this->context_->Update();
		this->renderer_->BeginFrame();
		this->renderer_->Clear(this->target_);
		this->context_->Render();
		this->renderer_->EndFrame(this->target_);
		this->check_errors();
	}

	void run()
	{
		if (this->running_)
		{
			this->render();
			this->window_->show();
		}

		while (this->running_)
		{
			MSG message{};

			while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
			{
				if (message.message == WM_QUIT)
				{
					this->running_ = false;
					break;
				}

				TranslateMessage(&message);
				DispatchMessageW(&message);

				if (!this->running_)
				{
					break;
				}
			}

			if (!this->running_)
			{
				break;
			}

			DWORD delay = 250;

			if (this->dimensions_.x > 0 && this->dimensions_.y > 0 && !IsIconic(*this->window_))
			{
				this->render();
				const auto result = this->swap_chain_->Present(1, 0);
				check_result(result, "Unable to present launcher graphics");

				if (result != DXGI_STATUS_OCCLUDED)
				{
					delay = static_cast<DWORD>(std::clamp(this->context_->GetNextUpdateDelay() * 1000.0, 1.0, 1000.0));
				}
			}

			MsgWaitForMultipleObjectsEx(0, nullptr, delay, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
		}

		if (this->error_)
		{
			std::rethrow_exception(this->error_);
		}
	}

	com_scope com_;
	system_interface system_;
	launcher_resources::file_interface files_;
	TextInputMethodEditor_Win32 text_input_;
	CComPtr<ID3D11Device> device_;
	CComPtr<ID3D11DeviceContext> device_context_;
	CComPtr<IDXGISwapChain> swap_chain_;
	CComPtr<ID3D11RenderTargetView> target_;
	std::unique_ptr<render_interface> renderer_;
	Rml::Context* context_{};
	std::function<void(Rml::Event&)> event_handler_;
	std::exception_ptr error_;
	Rml::Vector2i dimensions_{};
	bool initialized_{};
	bool resize_{true};
	bool running_{true};
	std::unique_ptr<window> window_;
};

rml_window::rml_window(const std::string& title, const int width, const int height) :
	impl_(std::make_unique<implementation>(title, width, height))
{
}

rml_window::~rml_window() = default;

Rml::ElementDocument* rml_window::load_document(const std::string& name)
{
	return this->impl_->load_document(name);
}

void rml_window::set_event_handler(std::function<void(Rml::Event&)> handler)
{
	this->impl_->event_handler_ = std::move(handler);
}

void rml_window::run() const
{
	this->impl_->run();
}

void rml_window::close()
{
	this->impl_->running_ = false;
}
