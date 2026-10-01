#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "callstack.hpp"
#include "console/console.hpp"

#include "steam/steam.hpp"
#include "resource.hpp"

#include <utils/cryptography.hpp>
#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/nt.hpp>

#include <zlib.h>

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

		utils::hook::detour playlist_checksum_hook;
		utils::hook::detour content_reset_hook;
		std::uint32_t steam_playlist_checksum{};
		std::uint32_t steam_playlist_version{};

		const char* get_platform_stub()
		{
			return "steam";
		}

		std::uint32_t get_playlist_checksum_stub()
		{
			const auto checksum = playlist_checksum_hook.invoke<std::uint32_t>();
			if (checksum == 0x1337 || checksum == 0x2002)
			{
				return checksum;
			}

			return steam_playlist_checksum;
		}

		std::uint32_t get_playlist_version_stub()
		{
			const auto version = *reinterpret_cast<std::uint32_t*>(0x14BEFB074_ms);
			return version && steam_playlist_version ? steam_playlist_version : version;
		}

		std::uint32_t parse_playlist_version(const std::string& playlists)
		{
			if (playlists.size() < 4)
			{
				return 0;
			}

			const auto offset = *reinterpret_cast<const std::uint16_t*>(playlists.data() + 2);
			if (offset >= playlists.size())
			{
				return 0;
			}

			char header[32]{};
			z_stream stream{};
			if (inflateInit(&stream) != Z_OK)
			{
				return 0;
			}

			stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(playlists.data() + offset));
			stream.avail_in = static_cast<uInt>(playlists.size() - offset);
			stream.next_out = reinterpret_cast<Bytef*>(header);
			stream.avail_out = sizeof(header) - 1;
			inflate(&stream, Z_SYNC_FLUSH);
			inflateEnd(&stream);

			std::uint32_t version{};
			if (sscanf_s(header, "version %u", &version) != 1)
			{
				return 0;
			}

			return version;
		}

		void patch_steam_netcode_version()
		{
			// Use the TU26 build bits in both netcode version builders
			utils::hook::set<std::uint32_t>(0x1406EFAED_ms, 0x344001E);
			utils::hook::set<std::uint32_t>(0x1406EFB03_ms, 0x344001E);

			utils::hook::set<std::uint32_t>(0x1406EFB36_ms, 0xFFFFFF1E);
			utils::hook::set<std::uint8_t>(0x1406EFB4C_ms, 0x1E);
			utils::hook::set<std::uint32_t>(0x1406EFB9F_ms, 0x3400000);

			// Report the checksum of the TU26 playlists the Steam clients use
			const auto playlists = utils::nt::load_resource(DW_PLAYLISTS);
			steam_playlist_checksum = static_cast<std::uint32_t>(crc32(0,
				reinterpret_cast<const Bytef*>(playlists.data()), static_cast<uInt>(playlists.size())));
			playlist_checksum_hook.create(0x1405DAB50_ms, get_playlist_checksum_stub);

			// Report the TU26 playlist version so hosts don't request a playlist download
			steam_playlist_version = parse_playlist_version(playlists);
			utils::hook::jump(0x1405E0D70_ms, get_playlist_version_stub);
		}

		void write_member_join_player_data_stub(void* party, void* msg, void* player_data)
		{
			utils::hook::invoke<void>(0x140411270_ms, party, msg, player_data);
			utils::hook::invoke<void>(0x1400BCD50_ms, msg, 0);
		}

		void read_member_join_player_data_stub(void* party, void* msg, void* player_data, const std::uint64_t xuid)
		{
			utils::hook::invoke<void>(0x14040EC20_ms, party, msg, player_data, xuid);

			const auto ticket_size = utils::hook::invoke<int>(0x1400BC1C0_ms, msg);
			if (ticket_size > 0)
			{
				std::array<std::uint8_t, 0x400> ticket{};
				utils::hook::invoke<void>(0x1400BBB70_ms, msg, ticket_size, ticket.data(),
					static_cast<int>(ticket.size()));
			}
		}

		void write_party_state_session_stub(void* msg, const void* data, const int size)
		{
			utils::hook::invoke<void>(0x1400BCAB0_ms, msg, data, size);

			constexpr std::uint64_t steam_lobby_id{};
			utils::hook::invoke<void>(0x1400BCAB0_ms, msg, &steam_lobby_id, static_cast<int>(sizeof(steam_lobby_id)));
		}

		void read_party_state_session_stub(void* msg, const int size, void* data, const int max_size)
		{
			utils::hook::invoke<void>(0x1400BBB70_ms, msg, size, data, max_size);

			std::uint64_t steam_lobby_id{};
			utils::hook::invoke<void>(0x1400BBB70_ms, msg, static_cast<int>(sizeof(steam_lobby_id)), &steam_lobby_id,
				static_cast<int>(sizeof(steam_lobby_id)));
		}

		void register_steam_content_packs()
		{
			void* table{};
			utils::hook::invoke<void>(0x1406EEB10_ms, "mp/ingamestore/s2_dlc_list.csv", &table);
			if (!table)
			{
				return;
			}

			const auto rows = std::min(utils::hook::invoke<int>(0x1406EEB90_ms, table), 0x100);
			for (auto row = 0; row < rows; ++row)
			{
				const auto* app_id = utils::hook::invoke<const char*>(0x1406EEB50_ms, table, row, 13);
				if (!app_id || !*app_id)
				{
					continue;
				}

				const auto pack_id = std::atoi(utils::hook::invoke<const char*>(0x1406EEB50_ms, table, row, 0));
				utils::hook::invoke<void>(0x1406F1450_ms, 0, pack_id, nullptr, 0, 0);
			}
		}

		void content_reset_stub()
		{
			content_reset_hook.invoke<void>();
			register_steam_content_packs();
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

		void report_fatal_error_stub(EXCEPTION_POINTERS*, const char* message, const bool terminate)
		{
			const std::string error = message ? message : "";
			const auto stack = callstack::capture();

			console::error("[Store] Fatal error: %s\n%s", error.data(), stack.data());
			utils::io::write_file("minidumps/s2x-fatal-error.txt", error + "\r\n\r\nCall stack:\r\n" + stack);

			if (terminate)
			{
				TerminateProcess(GetCurrentProcess(), 1);
			}
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

			// Resolve the GDK host module lookups to the loaded game image
			for (const auto address : {0x140714EA8_ms, 0x140714F3E_ms})
			{
				utils::hook::nop(address, 6);
				utils::hook::call(address, get_game_module);
			}

			utils::hook::call(0x14017D362_ms, query_game_license_result_stub);

			// Treat every content pack as owned
			utils::hook::set<std::uint32_t>(0x1406F19D0_ms, 0xC301B0);

			utils::hook::call(0x140A81FCB_ms, bd_log_stub);
			utils::hook::jump(0x1406BCFA0_ms, report_fatal_error_stub);

			// Authenticate with Demonware through the Steam ticket flow
			auth::patch();

			// Identify as the Steam platform and title
			utils::hook::jump(0x1401B0700_ms, get_platform_stub);
			utils::hook::set<std::uint32_t>(0x1401B0751_ms, 5597);
			utils::hook::copy_string(0x140B55250_ms, "s2_steam");
			utils::hook::copy_string(0x140BBAD10_ms, "bhs_s2_steam");

			// Skip the per-controller Store content pack enumeration
			utils::hook::set(0x14022C026_ms, std::array<std::uint8_t, 5>{0x31, 0xC0, 0x90, 0x90, 0x90});

			// Skip the COM security and WMI queries that stall startup
			utils::hook::set(0x140AD893F_ms, std::array<std::uint8_t, 6>{0xE9, 0xCD, 0x01, 0x00, 0x00, 0x90});

			patch_steam_netcode_version(); // matches TU26 netcode Steam latest does

			// register the Steam DLC packs so DLC playlists and maps are allowed
			content_reset_hook.create(0x1406F1790_ms, content_reset_stub);

			// TU26 appends a Steam auth ticket to pa_memberjoin player data
			utils::hook::call(0x1404213D2_ms, write_member_join_player_data_stub);
			utils::hook::call(0x14041299A_ms, read_member_join_player_data_stub);

			// ^ appends a Steam lobby id as well to the partystate session data
			utils::hook::call(0x14041FBED_ms, write_party_state_session_stub);
			utils::hook::call(0x14040351B_ms, read_party_state_session_stub);
		}
	};
}

REGISTER_COMPONENT(microsoft_store::component)
