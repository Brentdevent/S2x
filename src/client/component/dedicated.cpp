#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "console/console.hpp"
#include "dedicated_party.hpp"
#include "scheduler.hpp"

#include "game/game.hpp"

#include "component/gsc/script_extension.hpp"

#include <utils/flags.hpp>
#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace dedicated
{
	namespace
	{
		utils::hook::detour cl_check_for_resend_hook;
		utils::hook::detour db_release_upload_record_hook;
		utils::hook::detour scr_begin_load_scripts_hook;
		utils::hook::detour worker_dispatch_hook;

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
			utils::hook::invoke<void>(0x89D3C0_g, config.data());
			config[23] = 1;
			utils::hook::invoke<void>(0x89D620_g, config.data());
			utils::hook::invoke<void>(0x89E350_g, config.data());
		}

		void init_dedicated_graphics_restart_state()
		{
			// This is the existing-device branch of the stock renderer bootstrap.
			// CL_InitRenderer reaches it again after Com_ShutdownInternal rebuilds
			// frontend/DB state for a party-hosted match.
			utils::hook::invoke<void>(0x8D7A30_g);
			utils::hook::invoke<void>(0x898260_g);
			utils::hook::invoke<void>(0x8ABF50_g);
			utils::hook::invoke<void>(0x257060_g);
			utils::hook::invoke<void>(0x257C80_g);
			utils::hook::invoke<void>(0x8A0D30_g);
			utils::hook::invoke<void>(0x254710_g);
			utils::hook::invoke<void>(0x893790_g);
			utils::hook::invoke<void>(0x893820_g);
			utils::hook::invoke<void>(0x8B4740_g);
		}

		std::int64_t init_dedicated_graphics()
		{
			static bool initialized = false;
			if (!initialized)
			{
				initialized = true;
				console::info("Dedicated renderer: initializing headless graphics.\n");

				// Stock S2 creates this lock immediately after D3D device
				// creation. The headless path has no device, but DB/render-sync
				// workers still use the lock to drain CPU-side callbacks.
				auto& graphics_mutex = *reinterpret_cast<HANDLE*>(0xFB5838_g);
				if (!graphics_mutex)
				{
					graphics_mutex = CreateMutexA(nullptr, false, nullptr);
					if (!graphics_mutex)
					{
						game::Com_Error(game::ERR_FATAL,
							"Dedicated renderer synchronization initialization failed");
					}
				}

				*reinterpret_cast<bool*>(0xFFC90E0_g) = true;

				// The stock renderer bootstrap initializes the shared worker
				// command descriptors before any frontend or DB work can enqueue
				// commands. This is CPU-only and is required even without a GPU.
				utils::hook::invoke<void>(0x8D8E90_g);

				init_dedicated_video_config();

				// Initialize the renderer's CPU-owned state while skipping the
				// device validation branch. GPU resource creation is neutralized
				// by the dedicated callbacks installed in post_unpack().
				utils::hook::invoke<bool>(0x89A020_g, 1);

				// Successful D3D creation clears this flag. Leaving it set uses
				// the engine's native device-unavailable path, which prevents
				// resource accounting and command builders from touching absent
				// GPU allocations.
				*reinterpret_cast<bool*>(0xFB59E8_g) = true;

				// The native GPU-buffer initializer establishes a five-entry
				// frame-resource ring before allocating its D3D resources.
				// Preserve the CPU scheduling value without creating the rings.
				*reinterpret_cast<int*>(0xE34BE80_g) = 5;

				// This is the graphics-fastfile bootstrap in S2's native
				// renderer initialization.
				utils::hook::invoke<void>(0x94180_g);

				std::array<game::XZoneInfo, 8> zones{};
				unsigned int zone_count = 0;
				const auto zombies = utils::hook::invoke<bool>(0xB8CB0_g);

				add_graphics_zone(zones, zone_count,
					*reinterpret_cast<const char**>(0xFE1B6C0_g), 1);

				if (zombies)
				{
					add_graphics_zone(zones, zone_count,
						*reinterpret_cast<const char**>(0xFE1B6F0_g), 1);
				}

				add_graphics_zone(zones, zone_count,
					utils::hook::invoke<const char*>(0xA04E0_g), 4);
				add_graphics_zone(zones, zone_count,
					utils::hook::invoke<const char*>(0xA04D0_g), 4);
				add_graphics_zone(zones, zone_count,
					*reinterpret_cast<const char**>(0xFE1B6C8_g), 4);

				if (zombies)
				{
					add_graphics_zone(zones, zone_count,
						*reinterpret_cast<const char**>(0xFE1B6F8_g), 4);
				}

				add_graphics_zone(zones, zone_count,
					*reinterpret_cast<const char**>(0xFE1B6D0_g), 1);

				if (!zombies)
				{
					add_graphics_zone(zones, zone_count,
						*reinterpret_cast<const char**>(0xFE1B6D8_g), 2);
				}

				const auto virtual_lobby_enabled =
					*reinterpret_cast<game::dvar_t**>(0x14DBDE0_g);
				if (virtual_lobby_enabled && virtual_lobby_enabled->current.enabled &&
					utils::hook::invoke<bool>(0x9E050_g))
				{
					*reinterpret_cast<bool*>(0x1BD36FA_g) = true;
				}

				game::DB_LoadXAssets(zones.data(), zone_count, game::DB_LOAD_ASYNC);

				// Preserve the one-time portion of S2's native post-load order.
				utils::hook::invoke<void>(0x86F080_g);
				utils::hook::invoke<void>(0xE9E60_g);
			}

			init_dedicated_graphics_restart_state();

			// The stock renderer bootstrap closes the native splash here.
			utils::hook::invoke<void>(0x7B1590_g);
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
			utils::hook::set<game::BuiltinFunction>(0xAC9D310_g, dedicated_gsc_noop);
			utils::hook::set<game::BuiltinFunction>(0xAC9DB38_g, dedicated_gsc_noop);
		}

		void ensure_dedicated_render_command_pool()
		{
			if (!*reinterpret_cast<void**>(0x104F04A0_g))
			{
				// Renderer restart normally republishes this CPU-owned command
				// pool before DB upload commands can be recycled.
				utils::hook::invoke<void>(0x8DA810_g);
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
			auto result = utils::hook::invoke<int>(0xAC800_g);
			auto& upload_active = *reinterpret_cast<int*>(0x1BD3BD0_g);
			if (!upload_active)
			{
				return result;
			}

			// The stock path defers CPU-side asset publication behind a D3D
			// query. Preserve that one-boundary deferral so zone unloads can
			// restore overridden assets, but leave the absent GPU fence null.
			upload_active = 0;
			*reinterpret_cast<int*>(0x1BD3BD4_g) = 1;
			*reinterpret_cast<std::int64_t*>(0x1BD3BD8_g) = 0;
			return result;
		}

		void pump_dedicated_db_updates()
		{
			// The stock loading-screen frame advances this CPU-side preload
			// progress value. PartyHost waits for it before publishing the
			// callback that starts a preloaded map.
			utils::hook::invoke<void>(0x1FB120_g);

			// Renderer frames normally publish completed DB upload batches.
			// Dedicated mode has no render thread, so drain only work the native
			// DB state explicitly reports as pending.
			if (*reinterpret_cast<const int*>(0x1BD3BD4_g))
			{
				ensure_dedicated_render_command_pool();
				utils::hook::invoke<int>(0xAC800_g);
			}

			if (utils::hook::invoke<bool>(0x25EEB0_g) ||
				*reinterpret_cast<const int*>(0x7E86F88_g) ||
				utils::hook::invoke<int>(0xAC6A0_g) ||
				*reinterpret_cast<const int*>(0x7F2ECCC_g))
			{
				utils::hook::invoke<void>(0x25CD20_g);
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
			// The stock function combines dvar registration with audio-device setup.
			// Register its dvars so later sound code can safely observe an inactive backend.
			*reinterpret_cast<game::dvar_t**>(0xD8B0880_g) =
				game::Dvar_RegisterBool("546", false, game::DVAR_FLAG_SAVED);
			*reinterpret_cast<game::dvar_t**>(0xD8B0888_g) =
				game::Dvar_RegisterFloat("3558", 65535.0f, 0.0f, 65535.0f, game::DVAR_FLAG_SAVED);
			*reinterpret_cast<game::dvar_t**>(0xD8B0890_g) =
				game::Dvar_RegisterFloat("2394", 3276.8f, 0.0f, 65536.0f, game::DVAR_FLAG_SAVED);
			*reinterpret_cast<game::dvar_t**>(0xD8B0898_g) =
				game::Dvar_RegisterFloat("80", 0.5f, 0.0f, 5.0f, game::DVAR_FLAG_SAVED);
			*reinterpret_cast<game::dvar_t**>(0xD8B08A0_g) =
				game::Dvar_RegisterBool("3690", false, game::DVAR_FLAG_SAVED);
			*reinterpret_cast<game::dvar_t**>(0xD8B08A8_g) =
				game::Dvar_RegisterFloat("1597", 1.0f, 0.25f, 2.0f, game::DVAR_FLAG_SAVED);
			*reinterpret_cast<game::dvar_t**>(0xD8B08B0_g) =
				game::Dvar_RegisterBool("2113", false, game::DVAR_FLAG_NONE);

			*reinterpret_cast<int*>(0xD8B08BC_g) = 0;
			std::memset(reinterpret_cast<void*>(0xD8B08F8_g), 0, 0x20);
			std::memset(reinterpret_cast<void*>(0xD8B0918_g), 0, 0x190);
			std::memset(reinterpret_cast<void*>(0xD8B0AB0_g), 0, 0xC0);
			return 1;
		}

		std::int64_t init_dedicated_renderer_dvars()
		{
			constexpr auto flags = static_cast<game::DvarFlags>(0x44);
			*reinterpret_cast<game::dvar_t**>(0x7087070_g) =
				game::Dvar_RegisterFloat("705", 1.0f, 1.0f, 8.0f, flags);
			*reinterpret_cast<game::dvar_t**>(0x70892F0_g) =
				game::Dvar_RegisterFloat("2812", 16.0f, 0.0f, 32.0f, flags);
			return 0;
		}

		void disable_p2p_auth_ticket_validation()
		{
			constexpr std::array<std::uint8_t, 5> mark_authenticated{
				0xC6, 0x44, 0x24, 0x70, 0x01
			};

			utils::hook::copy(
				0x486E87_g,
				mark_authenticated.data(),
				mark_authenticated.size()
			);

			utils::hook::jump(0x486E8C_g, 0x486FA8_g);
			utils::hook::nop(0x486E91_g, 1);
		}

		std::uint8_t is_direct_connect_slot_reserved_stub(void* party_data, const char client_num)
		{
			// The persistent frontend owner remains party member 0, but it never creates
			// a gameplay client. Do not let that party entry reserve svs_clients[0].
			if (client_num == 0)
			{
				return 0;
			}

			return utils::hook::invoke<std::uint8_t>(0x6FE240_g, party_data, client_num);
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

			game::Cbuf_AddText(local_client, "exec default_xboxlive.cfg\n");
			queue_startup_config(local_client);
			game::Cbuf_AddText(local_client, "virtuallobby\n");
		}

		bool dedicated_frontend_ready()
		{
			constexpr auto breadcrumb_ddl = "mp/ddl/breadcrumbdata.ddl";

			return game::virtual_lobby_loaded() &&
				*game::databaseCompletedEvent2 &&
				utils::hook::invoke<void*>(0xA1C5C0_g, breadcrumb_ddl, 0);
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
			if (!game::environment::is_dedi())
			{
				return;
			}

			game::Dvar_RegisterBool("dedicated", true, game::DVAR_FLAG_READ);
			game::Dvar_RegisterBool("sv_lanOnly", false, game::DVAR_FLAG_NONE);

			// R_Init normally marks the renderer active after device creation.
			// Our dedicated bootstrap creates no device, so keep that state false
			// and let the native frontend paths skip renderer ownership waits.
			utils::hook::set<std::uint8_t>(0x899577_g, 0);

			// Dedicated servers never submit render commands. These are S2's
			// render-thread entry and renderer frame begin/end builders.
			utils::hook::set<std::uint8_t>(0x8E6CF0_g, 0xC3);
			utils::hook::set<std::uint8_t>(0x8BC640_g, 0xC3);
			utils::hook::set<std::uint8_t>(0x18C6C0_g, 0xC3);
			utils::hook::set<std::uint8_t>(0xF1A00_g, 0xC3);
			utils::hook::set<std::uint8_t>(0x8BBCD0_g, 0xC3);
			utils::hook::set<std::uint8_t>(0x8BC6F0_g, 0xC3);
			utils::hook::set<std::uint8_t>(0x8BB920_g, 0xC3);
			utils::hook::jump(0x8BB120_g, dedicated_noop);
			utils::hook::jump(0x8BB130_g, dedicated_noop);
			utils::hook::jump(0x8BB140_g, dedicated_noop);
			utils::hook::jump(0x8BB180_g, dedicated_noop);
			utils::hook::jump(0x8BB1C0_g, dedicated_noop);

			// S2's "4838" dvar is r_loadForRenderer. Register it disabled so
			// fastfile loading retains server assets without building GPU data.
			utils::hook::set<std::uint8_t>(0x88FF2A_g, 0);

			// S2's "86" dvar is r_preloadShaders. Shader preloading is completed
			// by the render thread, which does not exist in dedicated mode.
			utils::hook::set<std::uint8_t>(0x89014A_g, 0);

			// These native DB callbacks create GPU-backed buffers while loading
			// fastfiles. Dedicated gameplay only needs the server-side assets.
			utils::hook::set<std::uint8_t>(0x8AB8D0_g, 0xC3);
			utils::hook::set<std::uint8_t>(0x8AB970_g, 0xC3);
			utils::hook::set<std::uint8_t>(0x8AB9D0_g, 0xC3);
			utils::hook::jump(0x8AB460_g, dedicated_noop);

			// This streaming callback allocates a fixed-pool upload record and
			// fills it exclusively with D3D buffers and views. Its caller marks
			// the asset ready independently, so a headless server must not queue
			// records for a render consumer that does not exist.
			utils::hook::jump(0x8DAC80_g, dedicated_noop);

			// S2's fastfile loader can still request a texture resource after
			// renderer startup. Return the native no-resource result for both
			// the texture and its shader-resource view.
			utils::hook::jump(0x86A6E0_g, create_dedicated_texture);
			utils::hook::jump(0x86AB50_g, create_dedicated_texture);
			utils::hook::jump(0x86ACC0_g, create_dedicated_buffer);
			utils::hook::jump(0x86AD30_g, create_dedicated_dynamic_buffer);
			utils::hook::jump(0x86ADF0_g, dedicated_noop);
			utils::hook::jump(0x896620_g, create_dedicated_image_1d);
			utils::hook::jump(0x896780_g, create_dedicated_image_2d);
			utils::hook::jump(0x896990_g, create_dedicated_image_3d);
			utils::hook::jump(0x896B20_g, create_dedicated_image_array);
			utils::hook::jump(0x896D60_g, create_dedicated_image_cube);
			utils::hook::jump(0x8973A0_g, dedicated_noop);
			utils::hook::jump(0x89B4B0_g, dedicated_noop);
			utils::hook::jump(0x89CDD0_g, dedicated_noop);
			utils::hook::jump(0x8C2510_g, dedicated_noop);
			utils::hook::jump(0x8C5220_g, dedicated_noop);
			utils::hook::jump(0x257060_g, dedicated_noop);
			utils::hook::jump(0x8BC2A0_g, dedicated_noop);
			utils::hook::jump(0x8BEDA0_g, dedicated_noop);
			utils::hook::jump(0x86AED0_g, dedicated_noop);
			utils::hook::jump(0x86AFC0_g, dedicated_noop);
			utils::hook::jump(0x25B9C0_g, dedicated_noop);
			utils::hook::jump(0x24A380_g, dedicated_noop);
			utils::hook::jump(0x212180_g, dedicated_noop);
			utils::hook::jump(0x8CEFE0_g, dedicated_noop);
			utils::hook::jump(0x23B7E0_g, dedicated_noop);
			utils::hook::jump(0x8FDEE0_g, dedicated_noop);
			utils::hook::jump(0x23EE00_g, dedicated_noop);
			utils::hook::jump(0x8FCCE0_g, dedicated_noop);
			utils::hook::jump(0x8FC0F0_g, dedicated_noop);
			utils::hook::jump(0x19EB30_g, dedicated_noop);
			utils::hook::jump(0x8FF4A0_g, dedicated_noop);
			utils::hook::jump(0x251750_g, dedicated_noop);
			utils::hook::jump(0x251D30_g, dedicated_noop);
			utils::hook::jump(0x251DC0_g, dedicated_noop);
			utils::hook::jump(0x253750_g, init_dedicated_renderer_dvars);
			utils::hook::jump(0x1EB500_g, dedicated_noop);
			utils::hook::jump(0x86C0A0_g, dedicated_noop);
			utils::hook::jump(0x86CA20_g, dedicated_noop);
			utils::hook::jump(0x86B6A0_g, dedicated_noop);
			utils::hook::jump(0x862840_g, dedicated_noop);
			utils::hook::jump(0x8791B0_g, dedicated_noop);
			utils::hook::jump(0x8836C0_g, dedicated_noop);
			utils::hook::jump(0x240F70_g, dedicated_noop);

			// FX pass four only schedules particle simulation and render-vertex
			// worker commands. With no renderer workers those commands fill the
			// native queue and stall the main thread.
			utils::hook::jump(0x519A60_g, dedicated_noop);

			// G_RunFrame invokes this BFX simulation entry before running the
			// gameplay frame. Its task manager belongs to the absent renderer,
			// while the surrounding server frame must continue to run.
			utils::hook::jump(0x93DE90_g, dedicated_noop);

			// This scene submission pass only builds renderer light state and
			// queues its worker command. Dedicated mode has no scene or render
			// worker, so do not publish that command.
			utils::hook::jump(0xAB910_g, dedicated_noop);

			// This client-frame view builder assembles renderer scene, voxel,
			// and visibility state before publishing render workers. None of
			// that state is consumed by a dedicated gameplay server.
			utils::hook::jump(0xED2B0_g, dedicated_noop);

			// This follow-up view pass consumes the render snapshot produced by
			// the builder above. It is likewise presentation-only.
			utils::hook::jump(0x8B76E0_g, dedicated_noop);

			// This visibility wrapper publishes renderer worker commands and
			// builds scene-entity bitsets from the skipped view snapshot.
			utils::hook::jump(0xEEE10_g, dedicated_noop);

			// Presentation code can still request draw commands while the
			// dedicated frontend owns the persistent party. The native allocator
			// already returns null when its command list is full, and every caller
			// handles that result, so expose the same no-command result after the
			// headless path retires the render command list.
			utils::hook::jump(0x8BBE80_g, dedicated_noop);

			utils::hook::jump(0x8B8500_g, dedicated_noop);
			utils::hook::jump(0x8B8A90_g, dedicated_noop);
			utils::hook::jump(0x8B8B50_g, dedicated_noop);
			utils::hook::jump(0x8BB070_g, dedicated_noop);
			utils::hook::jump(0x8BB700_g, dedicated_noop);
			utils::hook::jump(0x8BB800_g, dedicated_noop);

			// Keep renderer jobs in the native worker queue so dependency and
			// completion accounting remains intact, but skip their presentation
			// handlers when a worker dispatches them.
			worker_dispatch_hook.create(0x8D99B0_g, dispatch_dedicated_worker_command);

			// Renderer validation calls this warning throttle when draw state is
			// incomplete. Its timing source belongs to the absent backend.
			utils::hook::jump(0x8D84F0_g, dedicated_noop);

			// This resets D3D immediate-context bindings after the renderer
			// memory-budget dvars are initialized. Keep the surrounding CPU
			// setup, but skip the context lock and GPU state updates.
			utils::hook::jump(0x8FE470_g, dedicated_noop);

			utils::hook::jump(0x8AB920_g, create_dedicated_resource_view);
			utils::hook::jump(0x8ABA20_g, create_dedicated_resource_view);
			utils::hook::jump(0x8ABA80_g, create_dedicated_resource_view);
			utils::hook::jump(0x8ABAE0_g, create_dedicated_resource_view);
			utils::hook::jump(0x8ABB60_g, create_dedicated_resource_view);
			utils::hook::jump(0x8ACB40_g, create_dedicated_inline_resource_view);
			utils::hook::jump(0x8ACBA0_g, create_dedicated_inline_resource_view);
			utils::hook::jump(0x8ACC00_g, create_dedicated_inline_resource_view);
			utils::hook::jump(0x8ACC80_g, create_dedicated_inline_resource_view);
			utils::hook::jump(0x8D1880_g, create_dedicated_input_layout);

			// These are S2's two D3D shader-view creators. S1x bypasses the
			// equivalent pair; clear the caller-owned views before returning.
			utils::hook::jump(0x2441F0_g, create_dedicated_formatted_buffer_unordered_view);
			utils::hook::jump(0x244290_g, create_dedicated_formatted_buffer_view);
			utils::hook::jump(0x244510_g, create_dedicated_shader_view);
			utils::hook::jump(0x2445A0_g, create_dedicated_shader_view);
			utils::hook::jump(0x2446F0_g, create_dedicated_formatted_buffer_unordered_view);
			utils::hook::jump(0x244780_g, create_dedicated_formatted_buffer_view);
			utils::hook::jump(0x249BD0_g, allocate_dedicated_resource_handle);

			// Dynamic upload command blocks own a family of D3D buffers and
			// views. Keep every output slot null so later CPU-side recycling
			// cannot mistake packed descriptor sentinels for COM interfaces.
			utils::hook::jump(0x86A6E0_g, clear_dedicated_resource);
			utils::hook::jump(0x86A7C0_g, clear_dedicated_resource);
			utils::hook::jump(0x86A7E0_g, clear_dedicated_resource);
			utils::hook::jump(0x86A810_g, clear_dedicated_resource);
			utils::hook::jump(0x86A830_g, clear_dedicated_resource);
			utils::hook::jump(0x86A8E0_g, clear_dedicated_resource);
			utils::hook::jump(0x86A910_g, clear_dedicated_resource);
			utils::hook::jump(0x86A950_g, clear_dedicated_resource);

			// The protected resource wrappers above can be restored while their
			// code is repacked. Patch the stable DB upload-record builder calls
			// as well, preserving its CPU layout while keeping every GPU output
			// slot null for the native recycler.
			utils::hook::call(0x8DAD57_g, clear_dedicated_resource);
			utils::hook::call(0x8DAD7B_g, clear_dedicated_resource);
			utils::hook::call(0x8DADC7_g, clear_dedicated_resource);
			utils::hook::call(0x8DADDA_g, clear_dedicated_resource);
			utils::hook::call(0x8DAE15_g, clear_dedicated_resource);
			utils::hook::call(0x8DAE3A_g, clear_dedicated_resource);
			utils::hook::call(0x8DAE5C_g, clear_dedicated_resource);
			utils::hook::call(0x8DAE77_g, clear_dedicated_resource);
			utils::hook::call(0x8DAEAD_g, clear_dedicated_resource);
			utils::hook::call(0x8DAEBD_g, clear_dedicated_resource);

			// Renderer teardown unconditionally releases these interfaces when
			// CPU-side world entries exist. Headless creation leaves the D3D
			// pointers null, so retain the native clearing semantics only.
			utils::hook::jump(0x86C180_g, clear_dedicated_resource);
			utils::hook::jump(0x86C1B0_g, release_dedicated_resource_pair);
			utils::hook::jump(0x86C900_g, release_dedicated_resource_pair);
			utils::hook::jump(0x25BF80_g, release_dedicated_buffer_allocations);

			// Client presentation converts UI rectangles through the physical
			// render dimensions. A headless server has no swap-chain dimensions.
			utils::hook::jump(0xE78A0_g, calculate_dedicated_screen_rect);

			// With no render device there is no render-thread acknowledgement
			// for remote-screen updates. Keep callers from waiting on it.
			utils::hook::set<std::uint8_t>(0x8BBC50_g, 0xC3);
			utils::hook::set<std::uint8_t>(0x8BBDB0_g, 0xC3);

			// This renderer-frame helper unconditionally dereferences ppDevice
			// to configure a DXGI interface. It has no server-side state.
			utils::hook::jump(0x899910_g, dedicated_noop);
			utils::hook::set<std::uint8_t>(0x89AD60_g, 0xC3);
			utils::hook::jump(0x89ADE0_g, dedicated_noop);
			utils::hook::jump(0x89AE90_g, dedicated_noop);

			// Complete DB upload batches without touching the absent D3D fence.
			db_release_upload_record_hook.create(0x8DA8B0_g, release_dedicated_upload_record);
			utils::hook::jump(0xAC6B0_g, finish_dedicated_db_upload_batch);
			scheduler::loop(pump_dedicated_db_updates, scheduler::pipeline::main);

			utils::hook::jump(0x7B23C0_g, init_dedicated_sound);

			// Install this last: once exposed, the renderer thread can enter the
			// replacement immediately and expects every GPU boundary above to be
			// neutralized already.
			utils::hook::jump(0x89AF80_g, init_dedicated_graphics);

			cl_check_for_resend_hook.create(game::CL_CheckForResend, cl_check_for_resend_stub);
			
			disable_p2p_auth_ticket_validation();

			// The persistent frontend leaves S2's virtual-lobby allocation flag set.
			// Force SV_Startup to use sv_maxClients instead of its 48-client frontend
			// allocation; gameplay client sidecars only contain 18 valid entries.
			utils::hook::set<std::uint8_t>(0x6DCE04_g, 0xEB);

			// SV_DirectConnect uses this party-member test while selecting a free
			// gameplay client. Preserve every reservation except the frontend owner.
			utils::hook::call(0xF3AA2_g, is_direct_connect_slot_reserved_stub);

			gsc::override_function("isusingmatchrulesdata", gscr_is_using_match_rules_data_stub);

			scr_begin_load_scripts_hook.create(0x6856D0_g, scr_begin_load_scripts_stub);

			// Bypass the gamestate guard
			utils::hook::nop(0xF44F3_g, 6);

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
