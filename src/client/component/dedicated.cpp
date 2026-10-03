#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "console/console.hpp"
#include "command.hpp"
#include "dedicated_party.hpp"
#include "scheduler.hpp"

#include "game/game.hpp"

#include "component/gsc/script_extension.hpp"

#include <utils/flags.hpp>
#include <utils/hook.hpp>
#include <utils/string.hpp>

#include <charconv>

namespace dedicated
{
	namespace
	{
		utils::hook::detour cl_check_for_resend_hook;
		utils::hook::detour com_quit_f_hook;
		utils::hook::detour db_release_upload_record_hook;
		utils::hook::detour gscr_set_slow_motion_hook;
		utils::hook::detour scr_begin_load_scripts_hook;
		utils::hook::detour worker_dispatch_hook;

		constexpr auto max_image_stream_file_count = 0x27400u;
		constexpr auto max_sound_stream_file_count = 0x3E80u;
		constexpr std::uint16_t invalid_image_stream_file_index = 191;
		constexpr std::uint16_t xpak_initialized_unavailable = 0x2000;
		constexpr auto default_net_port = 27016;
		constexpr std::array zone_load_event_offsets{0x2B8u, 0x2E0u, 0x308u};

		void* reset_dedicated_zone_load_state(void* const state, const int value, const size_t size)
		{
			// DB_TryLoadXFileInternal reuses this 0x5B0-byte record after the
			// preceding batch has completed. Its three OVERLAPPED readers retain
			// manual-reset events at these offsets, and the native memset otherwise
			// loses their handles on every full zone transition.
			for (const auto offset : zone_load_event_offsets)
			{
				auto& event = *reinterpret_cast<HANDLE*>(
					static_cast<std::uint8_t*>(state) + offset);
				if (event)
				{
					CloseHandle(event);
					event = nullptr;
				}
			}

			return std::memset(state, value, size);
		}

		void register_dedicated_net_port()
		{
			const auto value = utils::flags::get_set_value("net_port");
			if (!value)
			{
				return;
			}

			auto port = 0;
			const auto [end, error] = std::from_chars(
				value->data(), value->data() + value->size(), port);
			if (error != std::errc{} || end != value->data() + value->size()
				|| port < 1 || port > 65535)
			{
				console::warn(
					"Invalid +set net_port value '%s'; expected 1-65535, using %d.\n",
					value->data(), default_net_port);
				return;
			}

			// Native command-line commands execute after NET_OpenIP. Register
			// the latched dvar now so the native socket bind sees this value.
			game::Dvar_RegisterInt(
				"net_port", port, 0, 65535, game::DVAR_FLAG_LATCHED);
		}

		game::dvar_t* register_dedicated_data_validation_dvar(const char* name,
			int, const int min, const int max, const game::DvarFlags flags)
		{
			return game::Dvar_RegisterInt(name, 0, min, max, flags);
		}

		bool disable_dedicated_external_streams(game::FastfileExternalHeader* header,
			game::ExternalStreamFile* image_files, game::ExternalStreamFile* sound_files)
		{
			// Preserve S2's native header compatibility/cache transformation.
			const auto result = utils::hook::invoke<bool>(
				game::select(0x4A7440, 0x432B80), header, image_files, sound_files);
			if (!result)
			{
				return false;
			}

			// Leave malformed counts untouched so the native bounds checks report
			// the original fastfile error instead of allowing an oversized clear.
			if (header->imageFileCount > max_image_stream_file_count ||
				header->soundFileCount > max_sound_stream_file_count)
			{
				return true;
			}

			std::memset(image_files, 0,
				sizeof(*image_files) * header->imageFileCount);
			for (auto i = 0u; i < header->imageFileCount; ++i)
			{
				// S2's image-stream consumer treats index 191 as an absent
				// external payload and clears the corresponding stream state.
				image_files[i].fileIndex = invalid_image_stream_file_index;
			}

			// A zero sound descriptor is copied into the sound asset as an
			// undefined payload. Dedicated mode never initializes playback.
			std::memset(sound_files, 0,
				sizeof(*sound_files) * header->soundFileCount);
			return true;
		}

		std::uint32_t disable_dedicated_xpak_loading(void*, std::int64_t,
			std::uint8_t, int, std::uint16_t* flags)
		{
			// Mark the XPAK subsystem initialized but unavailable. The native
			// lookup masks this sentinel out, while DB initialization no longer
			// retries the missing TOCs for every zone.
			if (flags)
			{
				*flags = xpak_initialized_unavailable;
			}

			return 1;
		}

		void add_graphics_zone(std::array<game::XZoneInfo, 8>& zones, unsigned int& zone_count,
			const char* name, const int alloc_flags)
		{
			if (name)
			{
				zones[zone_count++] = {name, alloc_flags, 0};
			}
		}

		void init_dedicated_video_config()
		{
			// The stock renderer obtains these dimensions while enumerating DXGI
			// adapters. Dedicated mode still needs the resulting CPU-side screen
			// placement state, but must not create the factory, adapter, or device.
			alignas(16) std::array<std::uint32_t, 24> config{};
			config[6] = static_cast<std::uint32_t>(std::max(game::Dvar_GetInt("vid_width"), 300));
			config[7] = static_cast<std::uint32_t>(std::max(game::Dvar_GetInt("vid_height"), 300));

			// These native helpers derive the viewport, aspect ratio, and dynamic
			// resolution fields without allocating graphics resources.
			utils::hook::invoke<void>(game::select(0x89D3C0, 0x7FD2A0), config.data());
			config[23] = 1;
			utils::hook::invoke<void>(game::select(0x89D620, 0x7FD500), config.data());
			utils::hook::invoke<void>(game::select(0x89E350, 0x7FE260), config.data());
		}

		void init_dedicated_graphics_restart_state()
		{
			// This is the existing-device branch of the stock renderer bootstrap.
			// CL_InitRenderer reaches it again after Com_ShutdownInternal rebuilds
			// frontend/DB state for a party-hosted match.
			utils::hook::invoke<void>(game::select(0x8D7A30, 0x837920));
			utils::hook::invoke<void>(game::select(0x898260, 0x7F8210));
			utils::hook::invoke<void>(game::select(0x8ABF50, 0x80BE60));
			utils::hook::invoke<void>(game::select(0x257060, 0x205E80));
			utils::hook::invoke<void>(game::select(0x257C80, 0x206AA0));
			utils::hook::invoke<void>(game::select(0x8A0D30, 0x800C40));
			utils::hook::invoke<void>(game::select(0x254710, 0x203530));
			utils::hook::invoke<void>(game::select(0x893790, 0x7F3740));
			utils::hook::invoke<void>(game::select(0x893820, 0x7F37D0));
			utils::hook::invoke<void>(game::select(0x8B4740, 0x814650));
		}

		std::int64_t init_dedicated_graphics()
		{
			static bool initialized = false;
			if (!initialized)
			{
				initialized = true;

				// Stock S2 creates this lock immediately after D3D device
				// creation. The headless path has no device, but DB/render-sync
				// workers still use the lock to drain CPU-side callbacks.
				auto& graphics_mutex = *reinterpret_cast<HANDLE*>(game::select(0xFB5838, 0xFCB6F8));
				if (!graphics_mutex)
				{
					graphics_mutex = CreateMutexA(nullptr, false, nullptr);
					if (!graphics_mutex)
					{
						game::Com_Error(game::ERR_FATAL,
							"Dedicated renderer synchronization initialization failed");
					}
				}

				*reinterpret_cast<bool*>(game::select(0xFFC90E0, 0x116C0050)) = true;

				// The stock renderer bootstrap initializes the shared worker
				// command descriptors before any frontend or DB work can enqueue
				// commands. This is CPU-only and is required even without a GPU.
				utils::hook::invoke<void>(game::select(0x8D8E90, 0x838D80));

				init_dedicated_video_config();

				// Initialize the renderer's CPU-owned state while skipping the
				// device validation branch. GPU resource creation is neutralized
				// by the dedicated callbacks installed in post_unpack().
				utils::hook::invoke<bool>(game::select(0x89A020, 0x7F9F80), 1);

				// Successful D3D creation clears this flag. Leaving it set uses
				// the engine's native device-unavailable path, which prevents
				// resource accounting and command builders from touching absent
				// GPU allocations.
				*reinterpret_cast<bool*>(game::select(0xFB59E8, 0xFCB8A8)) = true;

				// The native GPU-buffer initializer establishes a five-entry
				// frame-resource ring before allocating its D3D resources.
				// Preserve the CPU scheduling value without creating the rings.
				*reinterpret_cast<int*>(game::select(0xE34BE80, 0xFA42E00)) = 5;

				// This is the graphics-fastfile bootstrap in S2's native
				// renderer initialization.
				utils::hook::invoke<void>(game::select(0x94180, 0x77590));

				std::array<game::XZoneInfo, 8> zones{};
				unsigned int zone_count = 0;
				const auto zombies = utils::hook::invoke<bool>(game::select(0xB8CB0, 0x98A20));

				add_graphics_zone(zones, zone_count,
					*reinterpret_cast<const char**>(game::select(0xFE1B6C0, 0x11512630)), 1);

				if (zombies)
				{
					add_graphics_zone(zones, zone_count,
						*reinterpret_cast<const char**>(game::select(0xFE1B6F0, 0x11512660)), 1);
				}

				add_graphics_zone(zones, zone_count,
					utils::hook::invoke<const char*>(game::select(0xA04E0, 0x80350)), 4);
				add_graphics_zone(zones, zone_count,
					utils::hook::invoke<const char*>(game::select(0xA04D0, 0x80340)), 4);
				add_graphics_zone(zones, zone_count,
					*reinterpret_cast<const char**>(game::select(0xFE1B6C8, 0x11512630)), 4);

				if (zombies)
				{
					add_graphics_zone(zones, zone_count,
						*reinterpret_cast<const char**>(game::select(0xFE1B6F8, 0x11512660)), 4);
				}

				add_graphics_zone(zones, zone_count,
					*reinterpret_cast<const char**>(game::select(0xFE1B6D0, 0x11512640)), 1);

				if (!zombies)
				{
					add_graphics_zone(zones, zone_count,
						*reinterpret_cast<const char**>(game::select(0xFE1B6D8, 0x11512640)), 2);
				}

				const auto virtual_lobby_enabled =
					*reinterpret_cast<game::dvar_t**>(game::select(0x14DBDE0, 0x1B6A030));
				if (virtual_lobby_enabled && virtual_lobby_enabled->current.enabled &&
					utils::hook::invoke<bool>(game::select(0x9E050, 0x7DE50)))
				{
					*reinterpret_cast<bool*>(game::select(0x1BD36FA, 0x1B69693)) = true;
				}

				game::DB_LoadXAssets(zones.data(), zone_count, game::DB_LOAD_ASYNC);

				// Preserve the one-time portion of S2's native post-load order.
				utils::hook::invoke<void>(game::select(0x86F080, 0x7CEFD0));
				utils::hook::invoke<void>(game::select(0xE9E60, 0xC8BB0));
			}

			init_dedicated_graphics_restart_state();

			// The stock renderer bootstrap closes the native splash here.
			utils::hook::invoke<void>(game::select(0x7B1590, 0x717A60));
			return 1;
		}

		bool create_dedicated_texture(std::int64_t* texture, const void*, int)
		{
			if (texture)
			{
				*texture = 0;
			}

			return false;
		}

		void clear_dedicated_image_resources(std::uint8_t* image)
		{
			if (image)
			{
				*reinterpret_cast<std::int64_t*>(image + 8) = 0;
				*reinterpret_cast<std::int64_t*>(image + 16) = 0;
				*reinterpret_cast<std::int64_t*>(image + 24) = 0;
			}
		}

		std::int64_t create_dedicated_image_1d(std::uint8_t* image, const std::int16_t width,
			const std::int64_t, const std::int64_t)
		{
			if (image)
			{
				*reinterpret_cast<std::int16_t*>(image + 76) = width;
				*reinterpret_cast<std::int16_t*>(image + 78) = 1;
				*reinterpret_cast<std::int16_t*>(image + 80) = 1;
				*(image + 88) = 2;
			}

			clear_dedicated_image_resources(image);
			return 0;
		}

		std::int64_t create_dedicated_image_2d(std::uint8_t* image, const std::int16_t width,
			const std::int16_t height, const int, const std::uint64_t, const int)
		{
			if (image)
			{
				*reinterpret_cast<std::int16_t*>(image + 76) = width;
				*reinterpret_cast<std::int16_t*>(image + 78) = height;
				*reinterpret_cast<std::int16_t*>(image + 80) = 1;
				*(image + 88) = 3;
			}

			clear_dedicated_image_resources(image);
			return 0;
		}

		std::int64_t create_dedicated_image_3d(std::uint8_t* image, const std::int16_t width,
			const std::int16_t height, const std::int16_t depth, const int, const std::int64_t)
		{
			if (image)
			{
				*reinterpret_cast<std::int16_t*>(image + 76) = width;
				*reinterpret_cast<std::int16_t*>(image + 78) = height;
				*reinterpret_cast<std::int16_t*>(image + 80) = depth;
				*(image + 88) = 4;
			}

			clear_dedicated_image_resources(image);
			return 0;
		}

		std::int64_t create_dedicated_image_cube(std::uint8_t* image, const std::int16_t size,
			const std::int64_t, const std::int64_t)
		{
			if (image)
			{
				*reinterpret_cast<std::int16_t*>(image + 76) = size;
				*reinterpret_cast<std::int16_t*>(image + 78) = size;
				*reinterpret_cast<std::int16_t*>(image + 80) = 1;
				*(image + 88) = 5;
			}

			clear_dedicated_image_resources(image);
			return 0;
		}

		std::int64_t create_dedicated_image_array(std::uint8_t* image, const std::int16_t width,
			const std::int16_t height, const std::int64_t, const int, const std::int64_t, const int)
		{
			if (image)
			{
				*reinterpret_cast<std::int16_t*>(image + 76) = width;
				*reinterpret_cast<std::int16_t*>(image + 78) = height;
				*reinterpret_cast<std::int16_t*>(image + 80) = 1;
				*(image + 88) = 6;
			}

			clear_dedicated_image_resources(image);
			return 0;
		}

		std::int64_t create_dedicated_resource_view(const void*, std::uint8_t* resource)
		{
			if (resource)
			{
				*reinterpret_cast<std::int64_t*>(resource + 8) = 0;
			}

			return 0;
		}

		std::int64_t create_dedicated_inline_resource_view(std::uint8_t* resource)
		{
			if (resource)
			{
				*reinterpret_cast<std::int64_t*>(resource + 8) = 0;
			}

			return 0;
		}

		std::int64_t create_dedicated_input_layout(const void*, int, const void*, const void*)
		{
			return 0;
		}

		std::int64_t dedicated_noop()
		{
			return 0;
		}

		void end_dedicated_frame()
		{
			// R_EndFrame normally submits the render command list before advancing
			// this two-bank, three-slot CPU scene ring. Dedicated mode skips the
			// submission, but must preserve the ring lifecycle for client frames.
			auto& scene_bank = *reinterpret_cast<unsigned int*>(game::select(0x1006E708, 0x11765678));
			scene_bank = (scene_bank - 1) & 1;
			*reinterpret_cast<std::uint64_t*>(game::select(0xFFC8D20, 0x116BFC90)) = 0;
		}

		bool is_dedicated_renderer_worker_command(const int command_type)
		{
			switch (command_type)
			{
			case 17:
			case 18:
			case 22:
			case 23:
			case 25:
			case 26:
			case 27:
			case 29:
			case 32:
			case 33:
			case 34:
			case 35:
			case 42:
				return true;
			default:
				return false;
			}
		}

		void dispatch_dedicated_worker_command(const int command_type, const void* data,
			const std::uint64_t signal)
		{
			if (!is_dedicated_renderer_worker_command(command_type))
			{
				worker_dispatch_hook.invoke<void>(command_type, data, signal);
			}
		}

		void dedicated_gsc_noop()
		{
		}

		void scr_begin_load_scripts_stub()
		{
			scr_begin_load_scripts_hook.invoke<void>();

			// Scr_BeginLoadScripts clears and rebuilds the native builtin table.
			// These local-player breadcrumb writers require frontend-owned DDL
			// that is intentionally absent once a headless server starts a map.
			utils::hook::set<game::BuiltinFunction>(game::select(0xAC9D310, 0xBFAA590), dedicated_gsc_noop);
			utils::hook::set<game::BuiltinFunction>(game::select(0xAC9DB38, 0xBFAADB8), dedicated_gsc_noop);
		}

		void gscr_set_slow_motion_stub()
		{
			gscr_set_slow_motion_hook.invoke<void>();

			// setslowmotion only publishes configstring 9. On a listen host the local
			// client applies it to the whole process, server included. A dedicated
			// server has no local client, so apply the same ramp on the main thread,
			// as a client would, with the builtin's defaults.
			const auto params = game::Scr_GetNumParam();
			const auto start = game::Scr_GetFloat(0);
			const auto end = params > 1 ? game::Scr_GetFloat(1) : 1.0f;
			const auto duration = params > 2 ? static_cast<int>(game::Scr_GetFloat(2) * 1000.0f) : 1000;

			scheduler::once([start, end, duration]
			{
				utils::hook::invoke<void>(game::select(0x9B620, 0x7B5D0), start, end, duration > 0 ? duration : 0);
			}, scheduler::pipeline::main);
		}

		void ensure_dedicated_render_command_pool()
		{
			if (!*reinterpret_cast<void**>(game::select(0x104F04A0, 0x11BE7420)))
			{
				// Renderer restart normally republishes this CPU-owned command
				// pool before DB upload commands can be recycled.
				utils::hook::invoke<void>(game::select(0x8DA810, 0x83A700));
			}
		}

		void clear_dedicated_upload_record_resources(const std::int64_t record)
		{
			if (!record)
			{
				return;
			}

			auto* resources = *reinterpret_cast<std::int64_t**>(record + 64);
			if (resources)
			{
				resources[8] = 0;
				resources[9] = 0;

				for (auto i = 0; i < 7; ++i)
				{
					resources[10 + i] = 0;
					resources[17 + i] = 0;
				}

				resources[25] = 0;
				resources[26] = 0;
				resources[28] = 0;
				resources[29] = 0;
			}

			auto* subresources = *reinterpret_cast<std::uint8_t**>(record + 56);
			const auto subresource_count = *reinterpret_cast<const std::uint16_t*>(record + 42);
			for (auto i = 0u; subresources && i < subresource_count; ++i)
			{
				auto* subresource = subresources + 48 * i;
				*reinterpret_cast<std::int64_t*>(subresource + 32) = 0;
				*reinterpret_cast<std::int64_t*>(subresource + 40) = 0;
			}
		}

		std::int64_t release_dedicated_upload_record(const std::int64_t record)
		{
			// Initial fastfile work can queue records before dedicated
			// post_unpack installs the headless GPU creators. Remove only their
			// GPU-owned fields before the native record cleanup and recycling.
			clear_dedicated_upload_record_resources(record);
			return db_release_upload_record_hook.invoke<std::int64_t>(record);
		}

		int finish_dedicated_db_upload_batch()
		{
			ensure_dedicated_render_command_pool();
			auto result = utils::hook::invoke<int>(game::select(0xAC800, 0x8C670));
			auto& upload_active = *reinterpret_cast<int*>(game::select(0x1BD3BD0, 0x1553138));
			if (!upload_active)
			{
				return result;
			}

			// The stock path defers CPU-side asset publication behind a D3D
			// query. Preserve that one-boundary deferral so zone unloads can
			// restore overridden assets, but leave the absent GPU fence null.
			upload_active = 0;
			*reinterpret_cast<int*>(game::select(0x1BD3BD4, 0x155313C)) = 1;
			*reinterpret_cast<std::int64_t*>(game::select(0x1BD3BD8, 0x1553140)) = 0;
			return result;
		}

		void pump_dedicated_db_updates()
		{
			// The stock loading-screen frame advances this CPU-side preload
			// progress value. PartyHost waits for it before publishing the
			// callback that starts a preloaded map.
			utils::hook::invoke<void>(game::select(0x1FB120, 0x1AA810));

			// Renderer frames normally publish completed DB upload batches.
			// Dedicated mode has no render thread, so drain only work the native
			// DB state explicitly reports as pending.
			if (*reinterpret_cast<const int*>(game::select(0x1BD3BD4, 0x155313C)))
			{
				ensure_dedicated_render_command_pool();
				utils::hook::invoke<int>(game::select(0xAC800, 0x8C670));
			}

			if (utils::hook::invoke<bool>(game::select(0x25EEB0, 0x20DCD0)) ||
				*reinterpret_cast<const int*>(game::select(0x7E86F88, 0x918CD88)) ||
				utils::hook::invoke<int>(game::select(0xAC6A0, 0x8C510)) ||
				*reinterpret_cast<const int*>(game::select(0x7F2ECCC, 0x9234ACC)))
			{
				utils::hook::invoke<void>(game::select(0x25CD20, 0x20BB40));
			}
		}

		std::int64_t create_dedicated_shader_view(const void*, std::int64_t* view, const void*)
		{
			if (view)
			{
				*view = 0;
			}

			return 0;
		}

		std::int64_t create_dedicated_formatted_buffer_view(const void*, const unsigned int,
			std::int64_t* view)
		{
			if (view)
			{
				*view = 0;
			}

			return 0;
		}

		std::int64_t create_dedicated_formatted_buffer_unordered_view(const void*,
			const unsigned int, const std::uint8_t, std::int64_t* view)
		{
			if (view)
			{
				*view = 0;
			}

			return 0;
		}

		std::int64_t create_dedicated_buffer(const void*, const void*, std::int64_t* buffer)
		{
			if (buffer)
			{
				*buffer = 0;
			}

			return 0;
		}

		std::int64_t create_dedicated_dynamic_buffer(const std::int64_t, const std::int64_t,
			const std::int64_t, const std::int64_t, const std::int64_t, std::int64_t* buffer)
		{
			if (buffer)
			{
				*buffer = 0;
			}

			return 0;
		}

		std::int64_t clear_dedicated_resource(std::int64_t* resource)
		{
			if (resource)
			{
				*resource = 0;
			}

			return 0;
		}

		std::int64_t release_dedicated_resource_pair(std::int64_t* resources)
		{
			if (resources)
			{
				resources[0] = 0;
				resources[1] = 0;
			}

			return 0;
		}

		std::int64_t release_dedicated_buffer_allocations(std::uint8_t* state)
		{
			if (!state)
			{
				return 0;
			}

			const auto allocation_count = *reinterpret_cast<const std::uint32_t*>(state + 56);
			auto* allocations = *reinterpret_cast<std::uint8_t**>(state + 2816);
			for (auto i = 0u; allocations && i < allocation_count; ++i)
			{
				// Each 64-byte allocation owns two size/accounting fields followed
				// by two ID3D11Buffer pointers. Headless creation leaves the
				// pointers null, so clear the complete GPU-owned tail without
				// invoking GetDesc or Release on an absent interface.
				std::memset(allocations + 64 * i + 32, 0, 32);
			}

			return 0;
		}

		std::uint16_t allocate_dedicated_resource_handle(const void*, void*, const void*)
		{
			// The stock descriptor-ring allocator uses this sentinel when no
			// presentation descriptor is available.
			return UINT16_MAX;
		}

		std::int64_t calculate_dedicated_screen_rect(std::int16_t* rect, const std::int16_t*)
		{
			if (rect)
			{
				std::memset(rect, 0, sizeof(*rect) * 4);
			}

			return 0;
		}

		std::int64_t init_dedicated_sound()
		{
			// Preserve the low-level driver dvars and inactive state without
			// creating an audio device.
			*reinterpret_cast<game::dvar_t**>(game::select(0xD8B0880, 0xEBE20E0)) =
				game::Dvar_RegisterBool("546", false, game::DVAR_FLAG_SAVED);
			*reinterpret_cast<game::dvar_t**>(game::select(0xD8B0888, 0xEBE20E8)) =
				game::Dvar_RegisterFloat("3558", 65535.0f, 0.0f, 65535.0f, game::DVAR_FLAG_SAVED);
			*reinterpret_cast<game::dvar_t**>(game::select(0xD8B0890, 0xEBE20F0)) =
				game::Dvar_RegisterFloat("2394", 3276.8f, 0.0f, 65536.0f, game::DVAR_FLAG_SAVED);
			*reinterpret_cast<game::dvar_t**>(game::select(0xD8B0898, 0xEBE20F8)) =
				game::Dvar_RegisterFloat("80", 0.5f, 0.0f, 5.0f, game::DVAR_FLAG_SAVED);
			*reinterpret_cast<game::dvar_t**>(game::select(0xD8B08A0, 0xEBE2100)) =
				game::Dvar_RegisterBool("3690", false, game::DVAR_FLAG_SAVED);
			*reinterpret_cast<game::dvar_t**>(game::select(0xD8B08A8, 0xEBE2108)) =
				game::Dvar_RegisterFloat("1597", 1.0f, 0.25f, 2.0f, game::DVAR_FLAG_SAVED);
			*reinterpret_cast<game::dvar_t**>(game::select(0xD8B08B0, 0xEBE2110)) =
				game::Dvar_RegisterBool("2113", false, game::DVAR_FLAG_NONE);

			*reinterpret_cast<int*>(game::select(0xD8B08BC, 0xEBE211C)) = 0;
			std::memset(reinterpret_cast<void*>(game::select(0xD8B08F8, 0xEBE2158)), 0, 0x20);
			std::memset(reinterpret_cast<void*>(game::select(0xD8B0918, 0xEBE2178)), 0, 0x190);
			std::memset(reinterpret_cast<void*>(game::select(0xD8B0AB0, 0xEBE2310)), 0, 0xC0);
			return 1;
		}

		int ignore_dedicated_music_state(game::hks::lua_State*)
		{
			return 0;
		}

		void ignore_dedicated_client_sound_update(unsigned int)
		{
		}

		std::uint32_t play_dedicated_sound_alias()
		{
			return UINT32_MAX;
		}

		std::int64_t init_dedicated_renderer_dvars()
		{
			constexpr auto flags = static_cast<game::DvarFlags>(0x44);
			*reinterpret_cast<game::dvar_t**>(game::select(0x7087070, 0x838CE70)) =
				game::Dvar_RegisterFloat("705", 1.0f, 1.0f, 8.0f, flags);
			*reinterpret_cast<game::dvar_t**>(game::select(0x70892F0, 0x838F0F0)) =
				game::Dvar_RegisterFloat("2812", 16.0f, 0.0f, 32.0f, flags);
			return 0;
		}

		void disable_p2p_auth_ticket_validation()
		{
			constexpr std::array<std::uint8_t, 5> mark_authenticated{
				0xC6, 0x44, 0x24, 0x70, 0x01
			};

			utils::hook::copy(
				game::select(0x486E87, 0x0),
				mark_authenticated.data(),
				mark_authenticated.size()
			);

			utils::hook::jump(game::select(0x486E8C, 0x0), game::select(0x486FA8, 0x412A03));
			utils::hook::nop(game::select(0x486E91, 0x0), 1);
		}

		void cl_check_for_resend_stub(const unsigned int local_client_num)
		{
			if (game::virtual_lobby_loaded())
			{
				// PartyHost_Frame requires the frontend owner to finish its virtual-lobby
				// connection before the native prematch state machine can start a match.
				cl_check_for_resend_hook.invoke<void>(local_client_num);
			}

			// Virtual-lobby shutdown clears the loaded flag before gameplay reconnects,
			// keeping the dedicated process out of gameplay server-client slots.
		}

		void kill_server()
		{
			auto* clients = *game::mp::svs_clients;
			if (clients)
			{
				for (auto i = 0; i < *game::sv_maxclients; ++i)
				{
					if (clients[i].state >= 3)
					{
						game::SV_SendServerCommand(&clients[i], game::SV_CMD_CAN_IGNORE,
							"%c \"%s\"", 'r', "EXE_ENDOFGAME");
					}
				}
			}

			com_quit_f_hook.invoke<void>();
		}

		void gscr_is_using_match_rules_data_stub()
		{
			game::Scr_AddInt(0);
		}

		void queue_startup_config(const int local_client)
		{
			const auto config = utils::flags::get_plus_value("exec");
			if (config)
			{
				console::info("Queueing dedicated startup config '%s'.\n", config->data());
				game::Cbuf_AddText(local_client, utils::string::va("exec %s\n", config->data()));
			}
		}

		void perform_online_game_init()
		{
			constexpr auto local_client = 0;

			game::Cbuf_AddText(local_client, "resetSplitscreenSignIn\n");
			game::Cbuf_AddText(local_client, "forcenosplitscreencontrol main_XBOXLIVE_3\n");

			game::Cbuf_AddText(local_client, "onlinegame 1\n");
			game::Cbuf_AddText(local_client, "systemlink 0\n");
			game::Cbuf_AddText(local_client, "splitscreen 0\n");
			game::Cbuf_AddText(local_client, "setgameprivatematch 0\n");

			if (game::environment::is_multiplayer())
			{
				game::Cbuf_AddText(local_client, "exec default_xboxlive.cfg\n");
			}

			queue_startup_config(local_client);
			game::Cbuf_AddText(local_client, "virtuallobby\n");
		}

		bool dedicated_frontend_ready()
		{
			constexpr auto breadcrumb_ddl = "mp/ddl/breadcrumbdata.ddl";

			return game::virtual_lobby_loaded() &&
				*game::databaseCompletedEvent2 &&
				utils::hook::invoke<void*>(game::select(0xA1C5C0, 0xA44550), breadcrumb_ddl, 0);
		}

		void run_startup()
		{
			perform_online_game_init();

			console::info("==================================\n");
			console::info("S2x Dedicated Server\n");
			console::info("==================================\n");
			
			console::set_title("S2x Dedicated Server");

			console::info("Waiting for the virtual lobby to initialize...\n");
			scheduler::schedule([]
			{
				if (!dedicated_frontend_ready())
				{
					return scheduler::cond_continue;
				}

				console::info("Virtual lobby initialized.\n");
				dedicated_party::start();
				return scheduler::cond_end;
			}, scheduler::pipeline::main, 100ms);
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::is_dedicated())
			{
				return;
			}

			register_dedicated_net_port();

			// MP and Zombies share this Com_Frame gate on an encoded CRT-pointer
			// reference that remains unset under Wine in headless mode. Skipping
			// CL_Frame strands deferred hub creation and the frontend's loopback
			// connection. Keep the native client frame and its initialization/active
			// checks; cl_check_for_resend_stub still prevents gameplay reconnects.
			utils::hook::nop(game::select(0x92944, 0x0), 2);
			
			game::Dvar_RegisterBool("dedicated", true, game::DVAR_FLAG_READ);
			game::Dvar_RegisterBool("sv_lanOnly", false, game::DVAR_FLAG_NONE);

			// S2's console split-screen heuristic marks every client sharing one
			// base address as split-screen. On a PC dedicated server, separate
			// clients can legitimately share an address through NAT. Keep the
			// stock connect/disconnect bookkeeping, but never set its split-screen
			// flag; _playerlogic otherwise sends cg_fovScale ("3078") as 0.75.
			utils::hook::set<std::uint32_t>(game::select(0xF70DF, 0xD5C8F), 0);

			// R_Init normally marks the renderer active after device creation.
			// Our dedicated bootstrap creates no device, so keep that state false
			// and let the native frontend paths skip renderer ownership waits.
			utils::hook::set<std::uint8_t>(game::select(0x899577, 0x7F9507), 0);

			// Cinematic initialization tail-calls the system thread creator after its
			// CPU-side state is ready. Dedicated servers never decode or present
			// cinematics, so return at the tail call and leave initialization intact.
			utils::hook::set<std::uint8_t>(game::select(0x86F100, 0x7CF050), 0xC3);

			// S2's "3841" dvar is dvl. Its native validation paths CRC registered
			// buffers whenever they change; a dedicated server does not need client
			// data-tamper reporting, so use the engine's existing disabled mode.
			utils::hook::call(game::select(0x1708F, 0xE8BF), register_dedicated_data_validation_dvar);

			// One frame path explicitly forces a two-kilobyte validation pass even
			// when dvl is disabled. It has no server-side consumer or enforcement.
			utils::hook::call(game::select(0x84903, 0x75BE2), dedicated_noop);

			// Dedicated servers never submit render commands. These are S2's
			// render-thread entry and renderer frame begin/end builders.
			utils::hook::set<std::uint8_t>(game::select(0x8E6CF0, 0x846BE0), 0xC3);
			utils::hook::set<std::uint8_t>(game::select(0x8BC640, 0x81C560), 0xC3);
			utils::hook::set<std::uint8_t>(game::select(0x18C6C0, 0x16AB30), 0xC3);
			utils::hook::set<std::uint8_t>(game::select(0xF1A00, 0xD0790), 0xC3);
			utils::hook::jump(game::select(0x8BBCD0, 0x81BBF0), end_dedicated_frame);
			utils::hook::set<std::uint8_t>(game::select(0x8BC6F0, 0x81C610), 0xC3);
			utils::hook::set<std::uint8_t>(game::select(0x8BB920, 0x81B840), 0xC3);
			utils::hook::jump(game::select(0x8BB120, 0x81B040), dedicated_noop);
			utils::hook::jump(game::select(0x8BB130, 0x81B050), dedicated_noop);
			utils::hook::jump(game::select(0x8BB140, 0x81B060), dedicated_noop);
			utils::hook::jump(game::select(0x8BB180, 0x81B0A0), dedicated_noop);
			utils::hook::jump(game::select(0x8BB1C0, 0x81B0E0), dedicated_noop);

			// S2's "4838" dvar is r_loadForRenderer. Register it disabled so
			// fastfile loading retains server assets without building GPU data.
			utils::hook::set<std::uint8_t>(game::select(0x88FF2A, 0x7EFEDA), 0);

			// Texture-filter dvars can change after startup, including when Proton
			// applies graphics settings. Keep the native sampler lookup table and
			// mip-bias update, but skip the 256-entry COM release/CreateSamplerState
			// loop: dedicated mode has no D3D device or sampler objects. Returning
			// through the native epilogue also preserves frame-update bookkeeping.
			utils::hook::jump(game::select(0x8CEC80, 0x82EB70), game::select(0x8CED81, 0x82EC71));

			// DB_LoadXFile validates every external image and sound descriptor
			// before the renderer/audio load flags can discard their payloads.
			// Convert them to native absent descriptors after S2 transforms the
			// header, while retaining the counts used by sequential consumers.
			utils::hook::call(game::select(0x4A5D7C, 0x4314BC), disable_dedicated_external_streams);

			// Release the completed fastfile readers' OVERLAPPED events before
			// DB_TryLoadXFileInternal clears and reuses their zone-load record.
			utils::hook::call(game::select(0xAD004, 0x8CE24), reset_dedicated_zone_load_state);

			// XSurfaceShared and GfxWorld post-link callbacks resolve renderer-only
			// geometry through XPAKs. With no TOCs, their fatal lookup re-enters DB
			// while its writer lock is held and deadlocks DB_FindXAssetHeader.
			// Dedicated gameplay uses collision/physics assets instead, so leave
			// these renderer stream records inactive and keep teardown balanced.
			utils::hook::call(game::select(0x4A5056, 0x430796), disable_dedicated_xpak_loading);
			utils::hook::jump(game::select(0xA0150, 0x7FFC0), dedicated_noop);
			utils::hook::jump(game::select(0xA0410, 0x80280), dedicated_noop);
			utils::hook::jump(game::select(0x198200, 0x174AE0), dedicated_noop);
			utils::hook::jump(game::select(0x198290, 0x174B70), dedicated_noop);

			// S2's "86" dvar is r_preloadShaders. Shader preloading is completed
			// by the render thread, which does not exist in dedicated mode.
			utils::hook::set<std::uint8_t>(game::select(0x89014A, 0x7F00FA), 0);

			// These native DB callbacks create GPU-backed buffers while loading
			// fastfiles. Dedicated gameplay only needs the server-side assets.
			utils::hook::set<std::uint8_t>(game::select(0x8AB8D0, 0x80B7E0), 0xC3);
			utils::hook::set<std::uint8_t>(game::select(0x8AB970, 0x80B880), 0xC3);
			utils::hook::set<std::uint8_t>(game::select(0x8AB9D0, 0x80B8E0), 0xC3);
			utils::hook::jump(game::select(0x8AB460, 0x80B370), dedicated_noop);

			// This streaming callback allocates a fixed-pool upload record and
			// fills it exclusively with D3D buffers and views. Its caller marks
			// the asset ready independently, so a headless server must not queue
			// records for a render consumer that does not exist.
			utils::hook::jump(game::select(0x8DAC80, 0x83AB70), dedicated_noop);

			// S2's fastfile loader can still request a texture resource after
			// renderer startup. Return the native no-resource result for both
			// the texture and its shader-resource view.
			utils::hook::jump(game::select(0x86A6E0, 0x7CA630), create_dedicated_texture);
			utils::hook::jump(game::select(0x86AB50, 0x7CAAA0), create_dedicated_texture);
			utils::hook::jump(game::select(0x86ACC0, 0x7CAC10), create_dedicated_buffer);
			utils::hook::jump(game::select(0x86AD30, 0x7CAC80), create_dedicated_dynamic_buffer);
			utils::hook::jump(game::select(0x86ADF0, 0x7CAD40), dedicated_noop);
			utils::hook::jump(game::select(0x896620, 0x7F65D0), create_dedicated_image_1d);
			utils::hook::jump(game::select(0x896780, 0x7F6730), create_dedicated_image_2d);
			utils::hook::jump(game::select(0x896990, 0x7F6940), create_dedicated_image_3d);
			utils::hook::jump(game::select(0x896B20, 0x7F6AD0), create_dedicated_image_array);
			utils::hook::jump(game::select(0x896D60, 0x7F6D10), create_dedicated_image_cube);
			utils::hook::jump(game::select(0x8973A0, 0x7F7350), dedicated_noop);
			utils::hook::jump(game::select(0x89B4B0, 0x7FB3E0), dedicated_noop);
			utils::hook::jump(game::select(0x89CDD0, 0x7FCCB0), dedicated_noop);
			utils::hook::jump(game::select(0x8C2510, 0x822420), dedicated_noop);
			utils::hook::jump(game::select(0x8C5220, 0x825110), dedicated_noop);
			utils::hook::jump(game::select(0x257060, 0x205E80), dedicated_noop);
			utils::hook::jump(game::select(0x8BC2A0, 0x81C1C0), dedicated_noop);
			utils::hook::jump(game::select(0x8BEDA0, 0x81ECC0), dedicated_noop);

			// Render-target creation is disabled above. Keep the surrounding
			// PMem lifecycle, but skip its four COM/image teardown calls.
			utils::hook::nop(game::select(0x8BEF70, 0x81EE90), 5);
			utils::hook::nop(game::select(0x8BEF87, 0x81EEA7), 5);
			utils::hook::nop(game::select(0x8BF07E, 0x81EF9E), 5);
			utils::hook::nop(game::select(0x8BF097, 0x81EFB7), 5);

			// Preserve native renderer shutdown, but skip its two unguarded
			// release blocks for device objects headless startup never creates.
			utils::hook::jump(game::select(0x89C82C, 0x7FC70C), game::select(0x89C840, 0x7FC720));
			utils::hook::jump(game::select(0x89C89B, 0x7FC77B), game::select(0x89C8CD, 0x7FC7AD));

			// Keep the surrounding render-state cleanup while skipping its
			// headless-only null manager destructors.
			utils::hook::nop(game::select(0x8C403C, 0x823F4C), 5);
			utils::hook::set<std::uint8_t>(game::select(0x8C404D, 0x823F5D), 0xC3);
			utils::hook::nop(game::select(0x25C18B, 0x20AFAB), 5);
			utils::hook::nop(game::select(0x24AF6B, 0x1F9D8B), 5);
			utils::hook::nop(game::select(0x24AF77, 0x1F9D97), 5);
			utils::hook::nop(game::select(0x257594, 0x2063B4), 5);

			// Renderer worker types 12 and 13 are never created headlessly, so
			// their stock shutdown acknowledgements can never arrive.
			utils::hook::nop(game::select(0x89E026, 0x7FDF36), 5);

			// Dynamic-buffer teardown unconditionally calls the absent D3D
			// immediate context. Retain its native state/resource cleanup.
			utils::hook::jump(game::select(0x86C3A2, 0x7CC2F2), game::select(0x86C3C4, 0x7CC314));

			utils::hook::jump(game::select(0x86AED0, 0x7CAE20), dedicated_noop);
			utils::hook::jump(game::select(0x86AFC0, 0x7CAF10), dedicated_noop);
			utils::hook::jump(game::select(0x25B9C0, 0x20A7E0), dedicated_noop);
			utils::hook::jump(game::select(0x24A380, 0x1F91A0), dedicated_noop);
			utils::hook::jump(game::select(0x212180, 0x1C0F60), dedicated_noop);
			utils::hook::jump(game::select(0x8CEFE0, 0x82EED0), dedicated_noop);
			utils::hook::jump(game::select(0x23B7E0, 0x1EA600), dedicated_noop);
			utils::hook::jump(game::select(0x8FDEE0, 0x85DDA0), dedicated_noop);
			utils::hook::jump(game::select(0x23EE00, 0x1EDC20), dedicated_noop);
			utils::hook::jump(game::select(0x8FCCE0, 0x85CBA0), dedicated_noop);
			utils::hook::jump(game::select(0x8FC0F0, 0x85BFB0), dedicated_noop);
			utils::hook::jump(game::select(0x19EB30, 0x17EE80), dedicated_noop);
			utils::hook::jump(game::select(0x8FF4A0, 0x85F360), dedicated_noop);
			utils::hook::jump(game::select(0x251750, 0x200570), dedicated_noop);
			utils::hook::jump(game::select(0x251D30, 0x200B50), dedicated_noop);
			utils::hook::jump(game::select(0x251DC0, 0x200BE0), dedicated_noop);
			utils::hook::jump(game::select(0x253750, 0x202570), init_dedicated_renderer_dvars);
			utils::hook::jump(game::select(0x1EB500, 0x19AB20), dedicated_noop);
			utils::hook::jump(game::select(0x86C0A0, 0x7CBFF0), dedicated_noop);
			utils::hook::jump(game::select(0x86CA20, 0x7CC970), dedicated_noop);
			utils::hook::jump(game::select(0x86B6A0, 0x7CB5F0), dedicated_noop);
			utils::hook::jump(game::select(0x862840, 0x7C2790), dedicated_noop);
			utils::hook::jump(game::select(0x8791B0, 0x7D90A0), dedicated_noop);
			utils::hook::jump(game::select(0x8836C0, 0x7E35C0), dedicated_noop);
			utils::hook::jump(game::select(0x240F70, 0x1EFD90), dedicated_noop);

			// FX pass four only schedules particle simulation and render-vertex
			// worker commands. With no renderer workers those commands fill the
			// native queue and stall the main thread.
			utils::hook::jump(game::select(0x519A60, 0x4A50F0), dedicated_noop);

			// G_RunFrame invokes this BFX simulation entry before running the
			// gameplay frame. Its task manager belongs to the absent renderer,
			// while the surrounding server frame must continue to run.
			utils::hook::jump(game::select(0x93DE90, 0x917450), dedicated_noop);

			// This scene submission pass only builds renderer light state and
			// queues its worker command. Dedicated mode has no scene or render
			// worker, so do not publish that command.
			utils::hook::jump(game::select(0xAB910, 0x8B780), dedicated_noop);

			// This client-frame view builder assembles renderer scene, voxel,
			// and visibility state before publishing render workers. None of
			// that state is consumed by a dedicated gameplay server.
			utils::hook::jump(game::select(0xED2B0, 0xCC030), dedicated_noop);

			// This follow-up view pass consumes the render snapshot produced by
			// the builder above. It is likewise presentation-only.
			utils::hook::jump(game::select(0x8B76E0, 0x817600), dedicated_noop);

			// This visibility wrapper publishes renderer worker commands and
			// builds scene-entity bitsets from the skipped view snapshot.
			utils::hook::jump(game::select(0xEEE10, 0xCDB90), dedicated_noop);

			// Presentation code can still request draw commands while the
			// dedicated frontend owns the persistent party. The native allocator
			// already returns null when its command list is full, and every caller
			// handles that result, so expose the same no-command result after the
			// headless path retires the render command list.
			utils::hook::jump(game::select(0x8BBE80, 0x81BDA0), dedicated_noop);

			utils::hook::jump(game::select(0x8B8500, 0x818420), dedicated_noop);
			utils::hook::jump(game::select(0x8B8A90, 0x8189B0), dedicated_noop);
			utils::hook::jump(game::select(0x8B8B50, 0x818A70), dedicated_noop);
			utils::hook::jump(game::select(0x8BB070, 0x81AF90), dedicated_noop);
			utils::hook::jump(game::select(0x8BB700, 0x81B620), dedicated_noop);
			utils::hook::jump(game::select(0x8BB800, 0x81B720), dedicated_noop);

			// Keep renderer jobs in the native worker queue so dependency and
			// completion accounting remains intact, but skip their presentation
			// handlers when a worker dispatches them.
			worker_dispatch_hook.create(game::select(0x8D99B0, 0x8398A0), dispatch_dedicated_worker_command);

			// Renderer validation calls this warning throttle when draw state is
			// incomplete. Its timing source belongs to the absent backend.
			utils::hook::jump(game::select(0x8D84F0, 0x8383E0), dedicated_noop);

			// This resets D3D immediate-context bindings after the renderer
			// memory-budget dvars are initialized. Keep the surrounding CPU
			// setup, but skip the context lock and GPU state updates.
			utils::hook::jump(game::select(0x8FE470, 0x85E330), dedicated_noop);

			// A frontend video-change notification can still be raised after
			// headless fastfile setup. Its stock handler rebuilds the swap chain
			// and dereferences the absent DXGI factory. The caller clears the
			// notification after this call; screen placement was initialized
			// above by init_dedicated_video_config().
			utils::hook::call(game::select(0x6BDB8, 0x556F8), dedicated_noop);

			utils::hook::jump(game::select(0x8AB920, 0x80B830), create_dedicated_resource_view);
			utils::hook::jump(game::select(0x8ABA20, 0x80B930), create_dedicated_resource_view);
			utils::hook::jump(game::select(0x8ABA80, 0x80B990), create_dedicated_resource_view);
			utils::hook::jump(game::select(0x8ABAE0, 0x80B9F0), create_dedicated_resource_view);
			utils::hook::jump(game::select(0x8ABB60, 0x80BA70), create_dedicated_resource_view);
			utils::hook::jump(game::select(0x8ACB40, 0x80CA50), create_dedicated_inline_resource_view);
			utils::hook::jump(game::select(0x8ACBA0, 0x80CAB0), create_dedicated_inline_resource_view);
			utils::hook::jump(game::select(0x8ACC00, 0x80CB10), create_dedicated_inline_resource_view);
			utils::hook::jump(game::select(0x8ACC80, 0x80CB90), create_dedicated_inline_resource_view);
			utils::hook::jump(game::select(0x8D1880, 0x831770), create_dedicated_input_layout);

			// These are S2's two D3D shader-view creators. S1x bypasses the
			// equivalent pair; clear the caller-owned views before returning.
			utils::hook::jump(game::select(0x2441F0, 0x1F3010), create_dedicated_formatted_buffer_unordered_view);
			utils::hook::jump(game::select(0x244290, 0x1F30B0), create_dedicated_formatted_buffer_view);
			utils::hook::jump(game::select(0x244510, 0x1F3330), create_dedicated_shader_view);
			utils::hook::jump(game::select(0x2445A0, 0x1F33C0), create_dedicated_shader_view);
			utils::hook::jump(game::select(0x2446F0, 0x1F3510), create_dedicated_formatted_buffer_unordered_view);
			utils::hook::jump(game::select(0x244780, 0x1F35A0), create_dedicated_formatted_buffer_view);
			utils::hook::jump(game::select(0x249BD0, 0x1F89F0), allocate_dedicated_resource_handle);

			// Dynamic upload command blocks own a family of D3D buffers and
			// views. Keep every output slot null so later CPU-side recycling
			// cannot mistake packed descriptor sentinels for COM interfaces.
			utils::hook::jump(game::select(0x86A6E0, 0x7CA630), clear_dedicated_resource);
			utils::hook::jump(game::select(0x86A7C0, 0x7CA710), clear_dedicated_resource);
			utils::hook::jump(game::select(0x86A7E0, 0x7CA730), clear_dedicated_resource);
			utils::hook::jump(game::select(0x86A810, 0x7CA760), clear_dedicated_resource);
			utils::hook::jump(game::select(0x86A830, 0x7CA780), clear_dedicated_resource);
			utils::hook::jump(game::select(0x86A8E0, 0x7CA830), clear_dedicated_resource);
			utils::hook::jump(game::select(0x86A910, 0x7CA860), clear_dedicated_resource);
			utils::hook::jump(game::select(0x86A950, 0x7CA8A0), clear_dedicated_resource);

			// The protected resource wrappers above can be restored while their
			// code is repacked. Patch the stable DB upload-record builder calls
			// as well, preserving its CPU layout while keeping every GPU output
			// slot null for the native recycler.
			utils::hook::call(game::select(0x8DAD57, 0x83AC47), clear_dedicated_resource);
			utils::hook::call(game::select(0x8DAD7B, 0x83AC6B), clear_dedicated_resource);
			utils::hook::call(game::select(0x8DADC7, 0x83ACB7), clear_dedicated_resource);
			utils::hook::call(game::select(0x8DADDA, 0x83ACCA), clear_dedicated_resource);
			utils::hook::call(game::select(0x8DAE15, 0x83AD05), clear_dedicated_resource);
			utils::hook::call(game::select(0x8DAE3A, 0x83AD2A), clear_dedicated_resource);
			utils::hook::call(game::select(0x8DAE5C, 0x83AD4C), clear_dedicated_resource);
			utils::hook::call(game::select(0x8DAE77, 0x83AD67), clear_dedicated_resource);
			utils::hook::call(game::select(0x8DAEAD, 0x83AD9D), clear_dedicated_resource);
			utils::hook::call(game::select(0x8DAEBD, 0x83ADAD), clear_dedicated_resource);

			// Renderer teardown unconditionally releases these interfaces when
			// CPU-side world entries exist. Headless creation leaves the D3D
			// pointers null, so retain the native clearing semantics only.
			utils::hook::jump(game::select(0x86C180, 0x7CC0D0), clear_dedicated_resource);
			utils::hook::jump(game::select(0x86C1B0, 0x7CC100), release_dedicated_resource_pair);
			utils::hook::jump(game::select(0x86C900, 0x7CC850), release_dedicated_resource_pair);
			utils::hook::jump(game::select(0x25BF80, 0x20ADA0), release_dedicated_buffer_allocations);

			// Client presentation converts UI rectangles through the physical
			// render dimensions. A headless server has no swap-chain dimensions.
			utils::hook::jump(game::select(0xE78A0, 0xC65B0), calculate_dedicated_screen_rect);

			// With no render device there is no render-thread acknowledgement
			// for remote-screen updates. Keep callers from waiting on it.
			utils::hook::set<std::uint8_t>(game::select(0x8BBC50, 0x81BB70), 0xC3);
			utils::hook::set<std::uint8_t>(game::select(0x8BBDB0, 0x81BCD0), 0xC3);

			// This renderer-frame helper unconditionally dereferences ppDevice
			// to configure a DXGI interface. It has no server-side state.
			utils::hook::jump(game::select(0x899910, 0x7F9870), dedicated_noop);
			utils::hook::set<std::uint8_t>(game::select(0x89AD60, 0x7FAC90), 0xC3);
			utils::hook::jump(game::select(0x89ADE0, 0x7FAD10), dedicated_noop);
			utils::hook::jump(game::select(0x89AE90, 0x7FADC0), dedicated_noop);

			// Complete DB upload batches without touching the absent D3D fence.
			db_release_upload_record_hook.create(game::select(0x8DA8B0, 0x83A7A0), release_dedicated_upload_record);
			utils::hook::jump(game::select(0xAC6B0, 0x8C520), finish_dedicated_db_upload_batch);
			scheduler::loop(pump_dedicated_db_updates, scheduler::pipeline::main);

			// Com_Init normally starts the complete sound system here. Keep only
			// the inert driver state above; no sound tables, device, or workers
			// are needed by a dedicated server. It sets the initialized flag after
			// this call, so keep that flag false to prevent SND_Update from running.
			utils::hook::call(game::select(0x987AC, 0x78702), init_dedicated_sound);
			utils::hook::set<std::uint32_t>(game::select(0x987B7, 0x7870D), 0);
			utils::hook::jump(game::select(0x7B23C0, 0x718580), init_dedicated_sound);

			// LUI SetMusicState calls directly into the uninitialized high-level
			// sound owner, bypassing the SND_Update initialized-state guard.
			utils::hook::jump(game::select(0x109970, 0xEB470), ignore_dedicated_music_state);

			// The client-frame sound update has no initialized-state check before
			// its looping/channel pass dereferences the absent high-level owner.
			utils::hook::jump(game::select(0x447720, 0x3D2E90), ignore_dedicated_client_sound_update);

			// Prevent presentation code owned by the persistent party frontend
			// from updating or playing aliases after sound initialization was
			// skipped. Alias playback uses UINT32_MAX as its failure sentinel.
			utils::hook::jump(game::select(0x468C90, 0x3F4770), dedicated_noop);
			utils::hook::jump(game::select(0x71AA40, 0x6A6000), play_dedicated_sound_alias);

			// Com_Quit calls the high-level sound shutdown routine at this site.
			// It must not tear down a subsystem that dedicated mode never starts.
			utils::hook::call(game::select(0x9A2BE, 0x7A26E), dedicated_noop);

			// Install this last: once exposed, the renderer thread can enter the
			// replacement immediately and expects every GPU boundary above to be
			// neutralized already.
			utils::hook::jump(game::select(0x89AF80, 0x7FAEB0), init_dedicated_graphics);

			cl_check_for_resend_hook.create(game::CL_CheckForResend, cl_check_for_resend_stub);
			com_quit_f_hook.create(game::Com_Quit_f, kill_server);
			command::add("killserver", kill_server);
			
			disable_p2p_auth_ticket_validation();

			// The persistent frontend leaves S2's virtual-lobby allocation flag set.
			// Force SV_Startup to use sv_maxClients instead of its 48-client frontend
			// allocation; gameplay client sidecars only contain 18 valid entries.
			utils::hook::set<std::uint8_t>(game::select(0x6DCE04, 0x668424), 0xEB);

			// SV_DirectConnect reserves a minimum of one local gameplay slot on listen
			// servers. Dedicated hosts live outside the gameplay range. Start invited
			// peers at 0 and other peers at private_slots, including the bot replacement
			// scan which shares r14d. Keep native session reservation checks intact.
			utils::hook::nop(game::select(0xF3A43, 0xD27A3), 4); // remove setz r14b (r14d is already zero)
			utils::hook::set<std::uint8_t>(game::select(0xF3A5F, 0xD27BF), 0); // cmp eax, 0
			utils::hook::set<std::uint32_t>(game::select(0xF3A61, 0xD27C1), 0); // mov eax, 0

			if (game::environment::is_multiplayer())
			{
				gsc::override_function("isusingmatchrulesdata", gscr_is_using_match_rules_data_stub);
			}

			scr_begin_load_scripts_hook.create(game::select(0x6856D0, 0x610CA0), scr_begin_load_scripts_stub);
			gscr_set_slow_motion_hook.create(game::select(0x58F5D0, 0x520C40), gscr_set_slow_motion_stub);

			// Bypass the gamestate guard
			utils::hook::nop(game::select(0xF44F3, 0xD3233), 6);

			// Headless renderer initialization returns before the initial
			// fastfile load completes. Executing a loose config before this flag
			// is set blocks the main thread in DB_FindXAssetHeader.
			scheduler::schedule([]
			{
				if (!*game::databaseCompletedEvent2)
				{
					return scheduler::cond_continue;
				}

				run_startup();
				return scheduler::cond_end;
			}, scheduler::pipeline::main, 100ms);
		}
	};
}

REGISTER_COMPONENT(dedicated::component)
