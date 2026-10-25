#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "console/console.hpp"
#include "imagefiles.hpp"

#include <utils/hook.hpp>

namespace imagefiles
{
	namespace
	{
		constexpr std::uint16_t zone_pak_index = 96;
		constexpr std::uint8_t imagefile_pak_type = 3;
		constexpr std::uint8_t imagefile_pak_kind = 2;

		struct zone_pak
		{
			alignas(16) char file[0x50]{};
			alignas(16) char info[0x108]{};
		};

		std::mutex zone_paks_mutex;
		std::string loading_zone;
		std::unordered_map<std::string, std::unique_ptr<zone_pak>> zone_paks;

		std::unique_ptr<zone_pak> open_zone_pak(const std::string& zone, const std::uint8_t type)
		{
			const auto name = zone + ".pak";

			const auto handle = game::mp::Sys_CreateFile(0, name.data());
			if (handle == INVALID_HANDLE_VALUE)
			{
				console::warn("[Imagefiles] Zone '%s' has no '%s', its images will not stream\n", zone.data(), name.data());
				return {};
			}

			auto pak = std::make_unique<zone_pak>();

			char file_name[0x40]{};
			strncpy_s(file_name, name.data(), _TRUNCATE);
			utils::hook::invoke<void>(0x1FB5A0_g, pak->file, handle, file_name);
			if (!utils::hook::invoke<bool>(0x1FB460_g, pak->file))
			{
				CloseHandle(handle);
				return {};
			}

			utils::hook::invoke<void>(0x4A7D90_g, pak->info, pak->file, imagefile_pak_kind, type);
			console::debug("[Imagefiles] Opened '%s'\n", name.data());
			return pak;
		}

		void* get_zone_pak_info(const std::uint8_t type)
		{
			std::lock_guard _(zone_paks_mutex);
			if (loading_zone.empty())
			{
				return nullptr;
			}

			auto pak = zone_paks.find(loading_zone);
			if (pak == zone_paks.end())
			{
				pak = zone_paks.emplace(loading_zone, open_zone_pak(loading_zone, type)).first;
			}

			return pak->second ? pak->second->info : nullptr;
		}
	}

	void set_loading_zone(const char* zone)
	{
		std::lock_guard _(zone_paks_mutex);
		loading_zone = zone ? zone : "";
	}

	void close_zone_pak(const char* zone)
	{
		if (!zone || !*zone)
		{
			return;
		}

		std::lock_guard _(zone_paks_mutex);
		const auto pak = zone_paks.find(zone);
		if (pak == zone_paks.end())
		{
			return;
		}

		if (pak->second)
		{
			utils::hook::invoke<void>(0x1FB320_g, pak->second->file); // closes the handle
		}

		zone_paks.erase(pak);
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

			const auto stub = utils::hook::assemble([](utils::hook::assembler& a)
			{
				const auto original = a.new_label();
				const auto no_pak = a.new_label();

				a.cmp(bx, zone_pak_index);
				a.jne(original);
				a.cmp(bpl, imagefile_pak_type);
				a.jne(original);

				a.pushad64();
				a.movzx(ecx, bpl);
				a.call_aligned(get_zone_pak_info);
				a.mov(qword_ptr(rsp, 0x48), rax); // saved rsi
				a.popad64();

				a.test(rsi, rsi);
				a.jz(no_pak);

				// marks the info as in use and stores it in the stream
				a.jmp(0xA0056_g);

				// clears the stream like a stream without data, the image keeps its resident mips
				a.bind(no_pak);
				a.jmp(0xA00A4_g);

				a.bind(original);
				a.movzx(r12d, bx);
				a.mov(rax, 0x1C455F0_g);
				a.jmp(0x9FF92_g);
			});

			utils::hook::nop(0x9FF87_g, 11);
			utils::hook::jump(0x9FF87_g, stub);
		}
	};
}

REGISTER_COMPONENT(imagefiles::component)
