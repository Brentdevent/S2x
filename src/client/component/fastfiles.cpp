#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "command.hpp"
#include "console/console.hpp"
#include "fastfiles.hpp"
#include "filesystem.hpp"
#include "mods.hpp"
#include "scheduler.hpp"

#include <utils/hook.hpp>
#include <utils/nt.hpp>
#include <utils/string.hpp>

namespace fastfiles
{
	namespace
	{
		// DB_FindXAssetEntry (0xA0FC0) reduces the name hash modulo this
		constexpr auto db_hash_table_size = 0x48800u;

		// Zone slots in g_zones (0x58360E0, stride 0x5B0)
		constexpr auto max_zone_slots = 0x421u;

		constexpr auto ui_zone_alloc_flags = 4;

		constexpr auto mod_zone_alloc_flags = 0x10000000;
		constexpr auto mod_zone_name = "mod";

		// makeshift database read (0xA0B00)
		class db_read_lock
		{
		public:
			db_read_lock()
			{
				InterlockedIncrement(game::mp::db_assetReadCount);
				while (*game::mp::db_assetWriteCount)
				{
					Sleep(0);
				}
			}

			~db_read_lock()
			{
				InterlockedDecrement(game::mp::db_assetReadCount);
			}

			db_read_lock(const db_read_lock&) = delete;
			db_read_lock& operator=(const db_read_lock&) = delete;
		};

		utils::hook::detour sys_create_file_hook;

		HANDLE open_zone_file(const std::filesystem::path& path)
		{
			return CreateFileW(path.wstring().data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
				FILE_FLAG_OVERLAPPED, nullptr);
		}

		std::string get_usermap_name(const std::string& filename)
		{
			auto name = std::filesystem::path(filename).stem().string();
			if (name.ends_with("_load"))
			{
				name.resize(name.size() - 5);
			}

			return name;
		}

		HANDLE try_open_zone_file(const std::filesystem::path& path)
		{
			std::error_code error;
			if (!std::filesystem::is_regular_file(path, error) || error)
			{
				return INVALID_HANDLE_VALUE;
			}

			const auto handle = open_zone_file(path);
			if (handle != INVALID_HANDLE_VALUE)
			{
				console::debug("[Fastfiles] Opening '%s'\n", path.generic_string().data());
			}

			return handle;
		}

		HANDLE find_custom_file(const char* filename)
		{
			if (!filename || !*filename || std::strpbrk(filename, "/\\:"))
			{
				return INVALID_HANDLE_VALUE;
			}

			const auto usermap = get_usermap_name(filename);

			for (const auto& search_path : filesystem::get_search_paths())
			{
				const std::filesystem::path root{ search_path };
				for (const auto& path : { root / "zone" / filename, root / "usermaps" / usermap / filename })
				{
					const auto handle = try_open_zone_file(path);
					if (handle != INVALID_HANDLE_VALUE)
					{
						return handle;
					}
				}
			}

			return try_open_zone_file(utils::nt::library{}.get_folder() / "usermaps" / usermap / filename);
		}

		HANDLE sys_create_file_stub(const int folder, const char* filename)
		{
			// Folder 0 is "zone\\" in the Sys_CreateFile folder table (0xD8896C0)
			const auto handle = folder == 0 ? find_custom_file(filename) : INVALID_HANDLE_VALUE;
			if (handle != INVALID_HANDLE_VALUE)
			{
				return handle;
			}

			return sys_create_file_hook.invoke<HANDLE>(folder, filename);
		}

		bool is_ui_zone(const char* name)
		{
			return name && (!std::strcmp(name, "ui_mp") || !std::strcmp(name, "ui_zm_mp"));
		}

		bool is_mod_zone_loaded()
		{
			return std::ranges::any_of(get_loaded_zones(), [](const zone_info& info)
			{
				return (info.flags & mod_zone_alloc_flags) != 0;
			});
		}

		bool should_add_mod_zone(const std::vector<game::XZoneInfo>& zones)
		{
			if (mods::get_loaded_mod().empty())
			{
				return false;
			}

			const auto has_ui_zone = std::ranges::any_of(zones, [](const game::XZoneInfo& zone)
			{
				return is_ui_zone(zone.name);
			});

			if (!has_ui_zone)
			{
				return false;
			}

			const auto frees_mod_zone = std::ranges::any_of(zones, [](const game::XZoneInfo& zone)
			{
				return (zone.freeFlags & mod_zone_alloc_flags) != 0;
			});

			if (!frees_mod_zone && is_mod_zone_loaded())
			{
				return false;
			}

			return exists(mod_zone_name);
		}

		bool skip_localized_zone = false;

		void update_skip_localized_zone(const char* name)
		{
			skip_localized_zone = false;
			if (!name || !*name)
			{
				return;
			}

			const auto* language = utils::hook::invoke<const char*>(0x72D2F0_g); // SEH_GetCurrentLanguageName
			const std::string localized_name = utils::string::va("%s_%s", language, name);
			if (exists(localized_name))
			{
				return;
			}

			console::debug("[Fastfiles] Zone '%s' has no '%s', not loading localized zones for it\n", name,
				localized_name.data());
			skip_localized_zone = true;
		}

		void db_load_xassets_stub(game::XZoneInfo* zones, const unsigned int count, const game::DBSyncMode sync_mode)
		{
			std::vector<game::XZoneInfo> data(zones, zones + count);

			for (auto& zone : data)
			{
				if (zone.freeFlags & ui_zone_alloc_flags)
				{
					zone.freeFlags |= mod_zone_alloc_flags;
				}
			}

			if (should_add_mod_zone(data))
			{
				console::info("[Fastfiles] Loading zone '%s' for mod '%s'\n", mod_zone_name, mods::get_loaded_mod().data());

				game::XZoneInfo info{};
				info.name = mod_zone_name;
				info.allocFlags = mod_zone_alloc_flags;
				info.freeFlags = 0;
				data.emplace_back(info);
			}

			game::DB_LoadXAssets(data.data(), static_cast<unsigned int>(data.size()), sync_mode);
		}

		game::mp::XZone* get_zone(const unsigned int index)
		{
			if (index >= max_zone_slots)
			{
				return nullptr;
			}

			return reinterpret_cast<game::mp::XZone*>(&game::g_zones[sizeof(game::mp::XZone) * index]);
		}

		void print_pool_usages()
		{
			int counts[game::ASSET_TYPE_COUNT]{};
			get_asset_counts(counts);

			auto total = 0;
			for (auto i = 0; i < game::ASSET_TYPE_COUNT; i++)
			{
				const auto type = static_cast<game::XAssetType>(i);
				const auto limit = get_pool_size(type);
				if (counts[i] == 0 && limit == 0)
				{
					continue;
				}

				total += counts[i];
				console::info("%-32s %6i / %i\n", game::DB_GetXAssetTypeName(type), counts[i], limit);
			}

			console::info("%i assets loaded\n", total);
		}
	}

	bool is_supported()
	{
		return game::environment::uses_multiplayer_binary() && !game::environment::is_dedicated();
	}

	bool exists(const std::string& zone)
	{
		if (!game::environment::uses_multiplayer_binary())
		{
			return false;
		}

		// Same folder as DB_LoadZone (0xACE30) uses, DB_IsLocalizedZone (0xA3BE0) zones open from folder 1
		const auto folder = utils::hook::invoke<int>(0xA3BE0_g, zone.data()) ? 1 : 0;
		const auto handle = sys_create_file_stub(folder, utils::string::va("%s.ff", zone.data()));
		if (handle == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		CloseHandle(handle);
		return true;
	}

	bool is_zone_loaded(const std::string& zone)
	{
		return std::ranges::any_of(get_loaded_zones(), [&](const zone_info& info)
		{
			return !_stricmp(info.name.data(), zone.data());
		});
	}

	bool try_load_zone(const std::string& zone, const int alloc_flags)
	{
		if (zone.empty() || !exists(zone))
		{
			return false;
		}

		game::XZoneInfo info{};
		info.name = zone.data();
		info.allocFlags = alloc_flags;
		info.freeFlags = 0;
		game::DB_LoadXAssets(&info, 1, game::DB_LOAD_ASYNC);
		return true;
	}

	std::string get_zone_name(const unsigned int index)
	{
		const auto* zone = get_zone(index);
		if (!zone)
		{
			return {};
		}

		return std::string(zone->name, strnlen(zone->name, sizeof(zone->name)));
	}

	std::vector<zone_info> get_loaded_zones()
	{
		std::vector<zone_info> result;

		const auto count = std::min(static_cast<unsigned int>(std::max(*game::mp::g_zoneCount, 0)), max_zone_slots);
		result.reserve(count);

		for (auto i = 0u; i < count; i++)
		{
			const auto index = game::mp::g_zoneHandles[i];
			const auto* zone = get_zone(index);
			if (!zone)
			{
				continue;
			}

			result.emplace_back(index, get_zone_name(index), zone->flags);
		}

		return result;
	}

	void enum_asset_entries(const game::XAssetType type, const std::function<void(game::XAssetEntry*)>& callback,
		const bool include_override)
	{
		db_read_lock lock;

		for (auto bucket = 0u; bucket < db_hash_table_size; bucket++)
		{
			for (auto i = game::mp::db_hashTable[bucket]; i;)
			{
				auto* entry = &game::mp::g_assetEntryPool[i];

				if (entry->asset.type == type)
				{
					callback(entry);

					if (include_override)
					{
						for (auto next = entry->nextOverride; next;)
						{
							auto* override_entry = &game::mp::g_assetEntryPool[next];
							callback(override_entry);
							next = override_entry->nextOverride;
						}
					}
				}

				i = entry->nextHash;
			}
		}
	}

	void get_asset_counts(int (&counts)[game::ASSET_TYPE_COUNT])
	{
		std::memset(counts, 0, sizeof(counts));

		db_read_lock lock;

		for (auto bucket = 0u; bucket < db_hash_table_size; bucket++)
		{
			for (auto i = game::mp::db_hashTable[bucket]; i;)
			{
				const auto* entry = &game::mp::g_assetEntryPool[i];

				for (const auto* current = entry; current;)
				{
					const auto type = static_cast<int>(current->asset.type);
					if (type >= 0 && type < game::ASSET_TYPE_COUNT)
					{
						counts[type]++;
					}

					current = current->nextOverride ? &game::mp::g_assetEntryPool[current->nextOverride] : nullptr;
				}

				i = entry->nextHash;
			}
		}
	}

	int get_pool_size(const game::XAssetType type)
	{
		if (type < 0 || type >= game::ASSET_TYPE_COUNT)
		{
			return 0;
		}

		return game::mp::g_poolSize[type];
	}

	void reload_mod_zone()
	{
		if (!is_supported())
		{
			return;
		}

		scheduler::once([]
		{
			game::XZoneInfo info{};
			info.freeFlags = mod_zone_alloc_flags;

			if (!mods::get_loaded_mod().empty() && exists(mod_zone_name))
			{
				console::info("[Fastfiles] Loading zone '%s' for mod '%s'\n", mod_zone_name, mods::get_loaded_mod().data());

				info.name = mod_zone_name;
				info.allocFlags = mod_zone_alloc_flags;
			}
			else if (!is_mod_zone_loaded())
			{
				return;
			}

			game::DB_LoadXAssets(&info, 1, game::DB_LOAD_ASYNC);
		}, scheduler::pipeline::main);
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::uses_multiplayer_binary())
			{
				return;
			}

			sys_create_file_hook.create(game::mp::Sys_CreateFile, sys_create_file_stub);

			// allow unsigned ff
			utils::hook::set<std::uint8_t>(0x4A6BC3_g, 0xEB);

			// ui group free and reload, and the post gfx group load, all call DB_LoadXAssets
			utils::hook::call(0x8DF12_g, db_load_xassets_stub);
			utils::hook::call(0x8DFCD_g, db_load_xassets_stub);
			utils::hook::call(0x89B38B_g, db_load_xassets_stub);

			// skip localized zones if map is zone is custom
			const auto skip_localized_stub = utils::hook::assemble([](utils::hook::assembler& a)
			{
				const auto skip = a.new_label();

				a.test(r13d, r13d);
				a.jnz(skip);

				a.pushad64();
				a.mov(rcx, rbp);
				a.call_aligned(update_skip_localized_zone);
				a.popad64();

				a.push(rax);
				a.mov(rax, &skip_localized_zone);
				a.cmp(byte_ptr(rax), 0);
				a.pop(rax);
				a.jnz(skip);

				a.jmp(0xA5A24_g);

				a.bind(skip);
				a.jmp(0xA5AC2_g);
			});

			utils::hook::nop(0xA5A1B_g, 9);
			utils::hook::jump(0xA5A1B_g, skip_localized_stub);

			command::add("loadzone", [](const command::params& params)
			{
				if (params.size() < 2)
				{
					console::info("usage: loadzone <zone> [allocFlags]\n");
					return;
				}

				const std::string name = params.get(1);
				const auto alloc_flags = params.size() > 2
					? static_cast<int>(std::strtol(params.get(2), nullptr, 0))
					: ui_zone_alloc_flags;

				if (!try_load_zone(name, alloc_flags))
				{
					console::warn("loadzone: zone '%s' could not be found\n", name.data());
					return;
				}

				console::info("Loading zone '%s' (flags 0x%X)\n", name.data(), alloc_flags);
			});

			command::add("poolUsages", []
			{
				print_pool_usages();
			});

			command::add("listZones", []
			{
				const auto zones = get_loaded_zones();
				for (const auto& zone : zones)
				{
					console::info("[%3u] %-48s flags 0x%X\n", zone.index, zone.name.data(), zone.flags);
				}

				console::info("%zu zones loaded\n", zones.size());
			});
		}
	};
}

REGISTER_COMPONENT(fastfiles::component)
