#pragma once

#include <cstddef>
#include <cstdint>

namespace steam
{
	struct encrypted_app_ticket_response final
	{
		enum { callback_id = 154 };

		int m_e_result;
	};

	struct get_auth_session_ticket_response
	{
		enum { callback_id = 163 };

		unsigned int m_h_auth_ticket;
		int m_e_result;
	};

#pragma pack(push, 4)
	struct validate_auth_ticket_response
	{
		static constexpr int callback_id = 143;
		uint64_t steam_id;
		int auth_session_response;
		uint64_t owner_steam_id;
	};
#pragma pack(pop)

	// MP's callback vtable (BAD430) reports 20 bytes; 82D950 reads ID/result at 0/8.
	static_assert(sizeof(validate_auth_ticket_response) == 20);
	static_assert(alignof(validate_auth_ticket_response) == 4);
	static_assert(offsetof(validate_auth_ticket_response, auth_session_response) == 8);
	static_assert(offsetof(validate_auth_ticket_response, owner_steam_id) == 12);

	class user
	{
	public:
		~user() = default;

		virtual int GetHSteamUser();
		virtual bool LoggedOn();
		virtual steam_id GetSteamID();

		virtual int InitiateGameConnection(void* pAuthBlob, int cbMaxAuthBlob, steam_id steamIDGameServer,
		                                   unsigned int unIPServer, unsigned short usPortServer, bool bSecure);
		virtual void TerminateGameConnection(unsigned int unIPServer, unsigned short usPortServer);
		virtual void TrackAppUsageEvent(steam_id gameID, int eAppUsageEvent, const char* pchExtraInfo = "");
		virtual bool GetUserDataFolder(char* pchBuffer, int cubBuffer);
		virtual void StartVoiceRecording();
		virtual void StopVoiceRecording();
		virtual int GetAvailableVoice(unsigned int* pcbCompressed, unsigned int* pcbUncompressed,
		                              unsigned int nUncompressedVoiceDesiredSampleRate);
		virtual int GetVoice(bool bWantCompressed, void* pDestBuffer, unsigned int cbDestBufferSize,
		                     unsigned int* nBytesWritten, bool bWantUncompressed, void* pUncompressedDestBuffer,
		                     unsigned int cbUncompressedDestBufferSize, unsigned int* nUncompressBytesWritten,
		                     unsigned int nUncompressedVoiceDesiredSampleRate);
		virtual int DecompressVoice(void* pCompressed, unsigned int cbCompressed, void* pDestBuffer,
		                            unsigned int cbDestBufferSize, unsigned int* nBytesWritten);
		virtual unsigned int GetVoiceOptimalSampleRate();
		virtual unsigned int GetAuthSessionTicket(void* pTicket, int cbMaxTicket, unsigned int* pcbTicket);
		virtual int BeginAuthSession(const void* pAuthTicket, int cbAuthTicket, steam_id steamID);
		virtual void EndAuthSession(steam_id steamID);
		virtual void CancelAuthTicket(unsigned int hAuthTicket);
		virtual unsigned int UserHasLicenseForApp(steam_id steamID, unsigned int appID);
		virtual bool BIsBehindNAT();
		virtual void AdvertiseGame(steam_id steamIDGameServer, unsigned int unIPServer, unsigned short usPortServer);
		virtual unsigned long long RequestEncryptedAppTicket(void* pUserData, int cbUserData);
		virtual bool GetEncryptedAppTicket(void* pTicket, int cbMaxTicket, unsigned int* pcbTicket);

		virtual int GetGameBadgeLevel(int nSeries, bool bFoil);
		virtual int GetPlayerSteamLevel();
		virtual uint64_t RequestStoreAuthURL(const char* pchRedirectURL);
		virtual bool BIsPhoneVerified();
		virtual bool BIsTwoFactorEnabled();
		virtual bool BIsPhoneIdentifying();
		virtual bool BIsPhoneRequiringVerification();

		static void initialize();
		static void shutdown();
	};
}
