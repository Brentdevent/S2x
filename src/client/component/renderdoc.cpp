#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include <utils/hook.hpp>

namespace renderdoc
{
	namespace
	{
		// AMD AGS 5.0 AGSGPUInfo, as shipped with the game (amd_ags_x64.dll 5.0.6).
		struct ags_gpu_info
		{
			int ags_version_major;
			int ags_version_minor;
			int ags_version_patch;
			int is_wack_compliant;
			const char* driver_version;
			const char* radeon_software_version;
			int num_devices;
			void* devices;
		};

		static_assert(sizeof(ags_gpu_info) == 0x30);
		static_assert(offsetof(ags_gpu_info, driver_version) == 0x10);
		static_assert(offsetof(ags_gpu_info, devices) == 0x28);

		utils::hook::detour ags_init_hook;
		utils::hook::detour d3d11_create_device_hook;

		int __cdecl ags_init_stub(void** context, const void* config, ags_gpu_info* info)
		{
			// RenderDoc rejects AGS 5.0 without filling the struct, but the game
			// reads the version strings even when agsInit fails.
			if (info)
			{
				*info = {};
				info->driver_version = "";
				info->radeon_software_version = "";
			}

			return ags_init_hook.invoke<int>(context, config, info);
		}

		HRESULT WINAPI d3d11_create_device_stub(IDXGIAdapter* adapter, const D3D_DRIVER_TYPE driver_type,
			const HMODULE software, const UINT flags, const D3D_FEATURE_LEVEL* feature_levels, const UINT num_feature_levels,
			const UINT sdk_version, ID3D11Device** device, D3D_FEATURE_LEVEL* feature_level, ID3D11DeviceContext** context)
		{
			// RenderDoc deliberately leaves devices created with this flag unwrapped,
			// then fails to create the game's swap chain for the unwrapped device.
			return d3d11_create_device_hook.invoke<HRESULT>(adapter, driver_type, software,
				flags & ~D3D11_CREATE_DEVICE_PREVENT_ALTERING_LAYER_SETTINGS_FROM_REGISTRY, feature_levels,
				num_feature_levels, sdk_version, device, feature_level, context);
		}

		void hook_import(utils::hook::detour& hook, const std::string& library, const char* function, void* stub)
		{
			// Detour the resolved function rather than the IAT slot: RenderDoc rewrites
			// the game's import entries with its own hooks whenever a library loads.
			auto* const entry = utils::nt::library{}.get_iat_entry(library, function);
			if (entry && *entry)
			{
				hook.create(*entry, stub);
			}
		}
	}

	struct component final : generic_component
	{
	public:
		void post_load() override
		{
			if (!utils::nt::library("renderdoc.dll"))
			{
				return;
			}

			hook_import(ags_init_hook, "amd_ags_x64.dll", "agsInit", ags_init_stub);
			hook_import(d3d11_create_device_hook, "d3d11.dll", "D3D11CreateDevice", d3d11_create_device_stub);
		}
	};
}

REGISTER_COMPONENT(renderdoc::component)
