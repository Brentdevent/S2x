#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "auth.hpp"

#include <game/game.hpp>

#include <utils/nt.hpp>
#include <utils/hook.hpp>
#include <utils/string.hpp>
#include <utils/smbios.hpp>
#include <utils/byte_buffer.hpp>
#include <utils/info_string.hpp>
#include <utils/cryptography.hpp>

namespace auth
{
	namespace
	{
		constexpr uint64_t splitscreen_guest_guid_flag = 1ull << 63;

		std::string get_hdd_serial()
		{
			DWORD serial{};
			if (!GetVolumeInformationA("C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0))
			{
				return {};
			}

			return utils::string::va("%08X", serial);
		}

		std::string get_hw_profile_guid()
		{
			HW_PROFILE_INFO info;
			if (!GetCurrentHwProfileA(&info))
			{
				return {};
			}

			return std::string{info.szHwProfileGuid, sizeof(info.szHwProfileGuid)};
		}

		std::string get_protected_data()
		{
			std::string input = "s2x-auth";

			DATA_BLOB data_in{}, data_out{};
			data_in.pbData = reinterpret_cast<uint8_t*>(input.data());
			data_in.cbData = static_cast<DWORD>(input.size());
			if (CryptProtectData(&data_in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_LOCAL_MACHINE,
			                     &data_out) != TRUE)
			{
				return {};
			}

			const auto size = std::min(data_out.cbData, 52ul);
			std::string result{reinterpret_cast<char*>(data_out.pbData), size};
			LocalFree(data_out.pbData);

			return result;
		}

		std::string get_key_entropy()
		{
			std::string entropy{};
			entropy.append(utils::smbios::get_uuid());
			entropy.append(get_hw_profile_guid());
			entropy.append(get_protected_data());
			entropy.append(get_hdd_serial());

			if (entropy.empty())
			{
				entropy.resize(32);
				utils::cryptography::random::get_data(entropy.data(), entropy.size());
			}

			return entropy;
		}

		utils::cryptography::ecc::key& get_key()
		{
			static auto key = utils::cryptography::ecc::generate_key(512, get_key_entropy());
			return key;
		}

		bool is_second_instance()
		{
			static const auto is_first = []
			{
				static utils::nt::handle<> mutex = CreateMutexA(nullptr, FALSE, "s2x_mutex");
				return mutex && GetLastError() != ERROR_ALREADY_EXISTS;
			}();

			return !is_first;
		}

		std::string serialize_connect_data(const std::vector<char>& data)
		{
			utils::byte_buffer buffer{};
			buffer.write_vector(data);

			return buffer.move_buffer();
		}
	}

	uint64_t get_guid()
	{
		static const auto guid = []() -> uint64_t
		{
			if (game::environment::is_dedicated() || is_second_instance())
			{
				return 0x110000100000000 | (::utils::cryptography::random::get_integer() & ~0x80000000);
			}

			// Stock S2 derives a guest XUID by setting bit 63 on the primary XUID.
			// Reserve that bit so the guest cannot collapse onto the primary identity.
			return get_key().get_hash() & ~splitscreen_guest_guid_flag;
		}();

		return guid;
	}

	struct component final : generic_component
	{
		void post_unpack() override
		{
			// Patch steam id bit check
			std::vector<std::pair<size_t, size_t>> patches{};
			const auto p = [&patches](const size_t a, const size_t b)
			{
				patches.emplace_back(a, b);
			};

			if (!game::environment::uses_multiplayer_binary())
			{
				p(game::select(0, 0, 0x4E70DD), game::select(0, 0, 0x4E70F6));
				p(game::select(0, 0, 0x4E7BFB), game::select(0, 0, 0x4E7C2E));
				p(game::select(0, 0, 0x4E7F43), game::select(0, 0, 0x4E7F86));
				p(game::select(0, 0, 0x5BA2D5), game::select(0, 0, 0x5BA301));
				p(game::select(0, 0, 0x5BB8BD), game::select(0, 0, 0x5BB90C));
				p(game::select(0, 0, 0x5BBD6A), game::select(0, 0, 0x5BBDBF));
				p(game::select(0, 0, 0x5BC16B), game::select(0, 0, 0x5BC1A1));
				p(game::select(0, 0, 0x5C90F6), game::select(0, 0, 0x5C9132));
			}
			else if (!game::environment::is_store_native())
			{
				p(game::select(0x1908A, 0x0), game::select(0x190D9, 0x0));
				p(game::select(0x1A553, 0x0), game::select(0x1A598, 0x0));
				p(game::select(0x1B61B, 0x0), game::select(0x1B64E, 0x0));
				p(game::select(0x785ECD, 0x0), game::select(0x785EE6, 0x0));
				p(game::select(0x7869FB, 0x0), game::select(0x786A2E, 0x0));
				p(game::select(0x786D73, 0x0), game::select(0x786DB6, 0x0));
				p(game::select(0x82D4D0, 0x0), game::select(0x82D527, 0x0));
				p(game::select(0x82E576, 0x0), game::select(0x82E5BB, 0x0));
				p(game::select(0x82FD79, 0x0), game::select(0x82FDD0, 0x0));
				p(game::select(0x830312, 0x0), game::select(0x83036F, 0x0));
				p(game::select(0x830548, 0x0), game::select(0x830588, 0x0));
				p(game::select(0x830B7B, 0x0), game::select(0x830BB1, 0x0));
				p(game::select(0x84D9CC, 0x0), game::select(0x84DA21, 0x0));
				p(game::select(0x84E1B5, 0x0), game::select(0x84E1EA, 0x0));
			}

			for (const auto& patch : patches)
			{
				utils::hook::jump(patch.first, patch.second);
			}
		}
	};
}

REGISTER_COMPONENT(auth::component)
