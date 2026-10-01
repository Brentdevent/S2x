#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "callstack.hpp"
#include "console/console.hpp"

#include "steam/steam.hpp"

#include <utils/cryptography.hpp>
#include <utils/hook.hpp>

namespace microsoft_store
{
	namespace
	{
		struct xstore_game_license
		{
			char sku_store_id[18];
			bool is_active;
			bool is_trial_owned_by_this_user;
			bool is_disc_license;
			bool is_trial;
			std::uint32_t trial_time_remaining_in_seconds;
			char trial_unique_id[64];
			std::int64_t expiration_date;
		};

		static_assert(offsetof(xstore_game_license, is_active) == 0x12);
		static_assert(offsetof(xstore_game_license, is_trial) == 0x15);

		const char* get_platform_stub()
		{
			return "steam";
		}

		HMODULE get_game_module(LPCSTR)
		{
			return reinterpret_cast<HMODULE>(game::get_base());
		}

		HRESULT query_game_license_result_stub(void* async, xstore_game_license* license)
		{
			const auto result = utils::hook::invoke<HRESULT>(0x14088ECEC_ms, async, license);
			if (FAILED(result))
			{
				console::warn("[Store] XStoreQueryGameLicenseResult failed (0x%08X)\n", static_cast<unsigned>(result));
			}

			license->is_active = true;
			license->is_trial = false;
			return S_OK;
		}

		void bd_log_stub(const char* status, const std::uint32_t error_code, const std::uint32_t transport_code,
			const char* transport_status)
		{
			if (status && !std::strcmp(status, "BD_FAILED"))
			{
				console::warn("[BD] BD_FAILED error %u, transport %s (%u), call stack:\n%s", error_code,
					transport_status ? transport_status : "unknown", transport_code, callstack::capture().data());
			}

			utils::hook::invoke<void>(0x140AA6420_ms, status, error_code, transport_code, transport_status);
		}

		namespace auth
		{
			constexpr std::uint32_t steam_request_task = 28;
			constexpr std::uint32_t steam_reply_task = 29;
			constexpr std::uint32_t ticket_size = 0x80;

#pragma pack(push, 1)
			struct steam_auth_token
			{
				char magic[32];
				char key[24];
				std::uint64_t user_id;
				char username[64];
			};
#pragma pack(pop)

			static_assert(sizeof(steam_auth_token) == 128);

			std::string ticket_key{};

			void request_token_stub(char* auth)
			{
				*reinterpret_cast<std::uint32_t*>(auth + 0x31A0) = 2;
				utils::hook::invoke<int>(0x140A82550_ms, auth, auth + 0x31A8);
			}

			int handle_reply_stub(char* auth)
			{
				if (*reinterpret_cast<std::uint32_t*>(auth + 0x14) != 0x2C)
				{
					return utils::hook::invoke<int>(0x140A82F30_ms, auth);
				}

				return utils::hook::invoke<int>(0x140A82030_ms, auth, steam_reply_task, ticket_size);
			}

			bool validate_task_stub(void*, const std::uint64_t expected, const std::uint64_t received)
			{
				return expected == steam_reply_task && received == steam_reply_task;
			}

			bool process_ticket_stub(void*, const std::uint32_t iv_seed, char* ticket, const std::uint32_t length)
			{
				if (length != ticket_size || ticket_key.size() != sizeof(steam_auth_token::key))
				{
					return false;
				}

				const auto iv = utils::cryptography::tiger::compute(
					std::string(reinterpret_cast<const char*>(&iv_seed), sizeof(iv_seed)));
				const auto decrypted = utils::cryptography::des3::decrypt(std::string(ticket, length), iv, ticket_key);
				if (decrypted.size() != length)
				{
					return false;
				}

				std::memcpy(ticket, decrypted.data(), length);
				return true;
			}

			bool process_extra_data_stub(void*, void*)
			{
				return true;
			}

			bool create_platform_data_stub(void* auth, void* json)
			{
				steam_auth_token token{};
				std::memcpy(token.magic, "S2x", 3);
				utils::cryptography::random::get_data(token.key, sizeof(token.key));
				token.user_id = steam::SteamUser()->GetSteamID().bits;
				strncpy_s(token.username, steam::SteamFriends()->GetPersonaName(), _TRUNCATE);

				ticket_key.assign(token.key, sizeof(token.key));
				const auto encoded = utils::cryptography::base64::encode(
					reinterpret_cast<const std::uint8_t*>(&token), sizeof(token));

				return utils::hook::invoke<bool>(0x140A81E60_ms, auth, json)
					&& utils::hook::invoke<bool>(0x140A505C0_ms, json, "token", encoded.data())
					&& utils::hook::invoke<bool>(0x140A501E0_ms, json, "extended_data", true);
			}

			void patch()
			{
				utils::hook::jump(0x140A82410_ms, request_token_stub);
				utils::hook::set<std::uint32_t>(0x140A82610_ms, steam_request_task);
				utils::hook::set(0x140A8292F_ms, std::array<std::uint8_t, 5>{0xB0, 0x01, 0x90, 0x90, 0x90});

				utils::hook::set<void*>(0x140C78B30_ms, handle_reply_stub);
				utils::hook::set<void*>(0x140C78B38_ms, validate_task_stub);
				utils::hook::set<void*>(0x140C78B40_ms, process_ticket_stub);
				utils::hook::set<void*>(0x140C78B48_ms, process_extra_data_stub);
				utils::hook::set<void*>(0x140C78B50_ms, create_platform_data_stub);
			}
		}
	}

	class component final : public multiplayer_component
	{
	public:
		void post_load() override
		{
			if (!game::environment::is_store_native())
			{
				return;
			}

			for (const auto address : {0x140714EA8_ms, 0x140714F3E_ms})
			{
				utils::hook::nop(address, 6);
				utils::hook::call(address, get_game_module);
			}

			utils::hook::call(0x14017D362_ms, query_game_license_result_stub);
			utils::hook::set<std::uint32_t>(0x1406F19D0_ms, 0xC301B0);
			utils::hook::call(0x140A81FCB_ms, bd_log_stub);
			auth::patch();

			utils::hook::jump(0x1401B0700_ms, get_platform_stub);
			utils::hook::set<std::uint32_t>(0x1401B0751_ms, 5597);
			utils::hook::copy_string(0x140B55250_ms, "s2_steam");
			utils::hook::copy_string(0x140BBAD10_ms, "bhs_s2_steam");
			utils::hook::set(0x14022C026_ms, std::array<std::uint8_t, 5>{0x31, 0xC0, 0x90, 0x90, 0x90});
			utils::hook::set(0x140AD893F_ms, std::array<std::uint8_t, 6>{0xE9, 0xCD, 0x01, 0x00, 0x00, 0x90});
		}
	};
}

REGISTER_COMPONENT(microsoft_store::component)
