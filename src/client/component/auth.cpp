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
			if (game::environment::is_dedi() || is_second_instance())
			{
				return 0x110000100000000 | (::utils::cryptography::random::get_integer() & ~0x80000000);
			}

			return get_key().get_hash();
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

			if (game::environment::is_sp())
			{
				p(0x4E70DD_g, 0x4E70F6_g);
				p(0x4E7BFB_g, 0x4E7C2E_g);
				p(0x4E7F43_g, 0x4E7F86_g);
				p(0x5BA2D5_g, 0x5BA301_g);
				p(0x5BB8BD_g, 0x5BB90C_g);
				p(0x5BBD6A_g, 0x5BBDBF_g);
				p(0x5BC16B_g, 0x5BC1A1_g);
				p(0x5C90F6_g, 0x5C9132_g);
			}
			else
			{
				p(0x1908A_g, 0x190D9_g);
				p(0x1A553_g, 0x1A598_g);
				p(0x1B61B_g, 0x1B64E_g);
				p(0x785ECD_g, 0x785EE6_g);
				p(0x7869FB_g, 0x786A2E_g);
				p(0x786D73_g, 0x786DB6_g);
				p(0x82D4D0_g, 0x82D501_g);
				p(0x82E576_g, 0x82E5BB_g);
				p(0x82FD81_g, 0x82FDD0_g);
				p(0x83031A_g, 0x83036F_g);
				p(0x830548_g, 0x830588_g);
				p(0x830B7B_g, 0x830BB1_g);
				p(0x84D9CC_g, 0x84DA21_g);
				p(0x84E1B5_g, 0x84E1EA_g);
			}

			for (const auto& patch : patches)
			{
				utils::hook::jump(patch.first, patch.second);
			}
		}
	};
}

REGISTER_COMPONENT(auth::component)
