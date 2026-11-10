#include <std_include.hpp>
#include "../steam.hpp"

#include "component/auth.hpp"
#include "component/scheduler.hpp"

namespace steam
{
	namespace
	{
		std::string auth_ticket;
		bool auth_callbacks_active{};
		unsigned int next_auth_ticket{};
		uint64_t next_auth_request{};
		std::unordered_map<unsigned int, uint64_t> pending_auth_tickets;
		std::unordered_map<uint64_t, uint64_t> pending_auth_sessions;

		steam_id generate_steam_id()
		{
			steam_id id{};
			id.bits = auth::get_guid();
			return id;
		}
	}

	int user::GetHSteamUser()
	{
		return 1;
	}

	bool user::LoggedOn()
	{
		return true;
	}

	steam_id user::GetSteamID()
	{
		static auto id = generate_steam_id();
		return id;
	}

	int user::InitiateGameConnection(void* pAuthBlob, int cbMaxAuthBlob, steam_id steamIDGameServer,
	                                 unsigned int unIPServer, unsigned short usPortServer, bool bSecure)
	{
		return 0;
	}

	void user::TerminateGameConnection(unsigned int unIPServer, unsigned short usPortServer)
	{
	}

	void user::TrackAppUsageEvent(steam_id gameID, int eAppUsageEvent, const char* pchExtraInfo)
	{
	}

	bool user::GetUserDataFolder(char* pchBuffer, int cubBuffer)
	{
		return false;
	}

	void user::StartVoiceRecording()
	{
	}

	void user::StopVoiceRecording()
	{
	}

	int user::GetAvailableVoice(unsigned int* pcbCompressed, unsigned int* pcbUncompressed,
	                            unsigned int nUncompressedVoiceDesiredSampleRate)
	{
		return 0;
	}

	int user::GetVoice(bool bWantCompressed, void* pDestBuffer, unsigned int cbDestBufferSize,
	                   unsigned int* nBytesWritten, bool bWantUncompressed, void* pUncompressedDestBuffer,
	                   unsigned int cbUncompressedDestBufferSize, unsigned int* nUncompressBytesWritten,
	                   unsigned int nUncompressedVoiceDesiredSampleRate)
	{
		return 2;
	}

	int user::DecompressVoice(void* pCompressed, unsigned int cbCompressed, void* pDestBuffer,
	                          unsigned int cbDestBufferSize, unsigned int* nBytesWritten)
	{
		return 0;
	}

	unsigned int user::GetVoiceOptimalSampleRate()
	{
		return 0;
	}

	unsigned int user::GetAuthSessionTicket(void* pTicket, int cbMaxTicket, unsigned int* pcbTicket)
	{
		if (pcbTicket)
		{
			*pcbTicket = 0;
		}

		if (!pTicket || cbMaxTicket < 1 || !pcbTicket)
		{
			return 0;
		}

		std::lock_guard lock{callbacks::mutex_};
		if (!auth_callbacks_active)
		{
			return 0;
		}

		if (++next_auth_ticket == 0)
		{
			++next_auth_ticket;
		}

		const auto ticket = next_auth_ticket;
		const auto request = ++next_auth_request;
		pending_auth_tickets[ticket] = request;
		*static_cast<unsigned char*>(pTicket) = 0;
		*pcbTicket = 1;

		scheduler::once([ticket, request]
		{
			std::lock_guard lock{callbacks::mutex_};
			const auto pending = pending_auth_tickets.find(ticket);
			if (!auth_callbacks_active || pending == pending_auth_tickets.end() || pending->second != request)
			{
				return;
			}

			pending_auth_tickets.erase(pending);
			get_auth_session_ticket_response response{ticket, 1}; // k_EResultOK
			callbacks::dispatch_callback(&response, sizeof(response), response.callback_id);
		}, scheduler::main);

		return ticket;
	}

	int user::BeginAuthSession(const void* pAuthTicket, int cbAuthTicket, steam_id steamID)
	{
		std::lock_guard lock{callbacks::mutex_};
		if (!auth_callbacks_active || !pAuthTicket || cbAuthTicket < 1 || !steamID.bits)
		{
			return 1; // k_EBeginAuthSessionResultInvalidTicket
		}

		const auto request = ++next_auth_request;
		pending_auth_sessions[steamID.bits] = request;
		scheduler::once([steamID, request]
		{
			// Serialize completion with EndAuthSession and shutdown, including calls
			// made by a callback itself. Use the dispatcher's lock to avoid lock inversion.
			std::lock_guard lock{callbacks::mutex_};
			const auto pending = pending_auth_sessions.find(steamID.bits);
			if (!auth_callbacks_active || pending == pending_auth_sessions.end() || pending->second != request)
			{
				return;
			}

			pending_auth_sessions.erase(pending);
			// S2x emulates tickets and player IDs. Complete that handshake through
			// the native callback; this is not validation against Steam's backend.
			validate_auth_ticket_response response{steamID.bits, 0, steamID.bits};
			callbacks::dispatch_callback(&response, sizeof(response), response.callback_id);
		}, scheduler::main);

		return 0;
	}

	void user::EndAuthSession(steam_id steamID)
	{
		std::lock_guard lock{callbacks::mutex_};
		pending_auth_sessions.erase(steamID.bits);
	}

	void user::CancelAuthTicket(unsigned int hAuthTicket)
	{
		std::lock_guard lock{callbacks::mutex_};
		pending_auth_tickets.erase(hAuthTicket);
	}

	void user::initialize()
	{
		std::lock_guard lock{callbacks::mutex_};
		pending_auth_tickets.clear();
		pending_auth_sessions.clear();
		auth_callbacks_active = true;
	}

	void user::shutdown()
	{
		std::lock_guard lock{callbacks::mutex_};
		auth_callbacks_active = false;
		pending_auth_tickets.clear();
		pending_auth_sessions.clear();
	}

	unsigned int user::UserHasLicenseForApp(steam_id steamID, unsigned int appID)
	{
		return 1;
	}

	bool user::BIsBehindNAT()
	{
		return false;
	}

	void user::AdvertiseGame(steam_id steamIDGameServer, unsigned int unIPServer, unsigned short usPortServer)
	{
	}

	unsigned long long user::RequestEncryptedAppTicket(void* pUserData, int cbUserData)
	{
		const auto id = this->GetSteamID();

		auth_ticket = "S2x";
		auth_ticket.resize(32);
		auth_ticket.append(static_cast<char*>(pUserData), 24); // key
		auth_ticket.append(reinterpret_cast<const char*>(&id.bits), sizeof(id.bits)); // user id
		auth_ticket.append(&static_cast<char*>(pUserData)[24], 64); // user name

		// Create the call response
		const auto result = callbacks::register_call();
		const auto retvals = static_cast<encrypted_app_ticket_response*>(calloc(
			1, sizeof(encrypted_app_ticket_response)));
		//::Utils::Memory::AllocateArray<EncryptedAppTicketResponse>();
		retvals->m_e_result = 1;

		// Return the call response
		callbacks::return_call(retvals, sizeof(encrypted_app_ticket_response),
		                       encrypted_app_ticket_response::callback_id, result);

		return result;
	}

	bool user::GetEncryptedAppTicket(void* pTicket, int cbMaxTicket, unsigned int* pcbTicket)
	{
		if (cbMaxTicket < 0 || auth_ticket.empty()) return false;

		const auto size = auth_ticket.size();
		if (size_t(cbMaxTicket) < size)
		{
			*pcbTicket = static_cast<unsigned>(size);
			return false;
		}

		std::memcpy(pTicket, auth_ticket.data(), size);
		*pcbTicket = static_cast<unsigned>(size);

		return true;
	}

	int user::GetGameBadgeLevel(int nSeries, bool bFoil)
	{
		return 1;
	}

	int user::GetPlayerSteamLevel()
	{
		return 0;
	}

	uint64_t user::RequestStoreAuthURL(const char* pchRedirectURL)
	{
		return 0;
	}

	bool user::BIsPhoneVerified()
	{
		return true;
	}

	bool user::BIsTwoFactorEnabled()
	{
		return true;
	}

	bool user::BIsPhoneIdentifying()
	{
		return false;
	}

	bool user::BIsPhoneRequiringVerification()
	{
		return false;
	}
}
