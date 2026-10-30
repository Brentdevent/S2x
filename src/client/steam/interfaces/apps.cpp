#include <std_include.hpp>
#include "../steam.hpp"
#include "game/game.hpp"

namespace steam
{
	namespace
	{
		std::optional<game::environment::mode> get_app_mode(const unsigned int app_id)
		{
			switch (app_id)
			{
			case 476600:
				return game::environment::mode::singleplayer;
			case 476620:
				return game::environment::mode::multiplayer;
			default:
				return std::nullopt;
			}
		}
	}

	bool apps::BIsSubscribed()
	{
		return true;
	}

	bool apps::BIsLowViolence()
	{
		return false;
	}

	bool apps::BIsCybercafe()
	{
		return false;
	}

	bool apps::BIsVACBanned()
	{
		return false;
	}

	const char* apps::GetCurrentGameLanguage()
	{
		return "english";
	}

	const char* apps::GetAvailableGameLanguages()
	{
		return "english";
	}

	bool apps::BIsSubscribedApp(unsigned int appID)
	{
		return true;
	}

	bool apps::BIsDlcInstalled(unsigned int appID)
	{
		return true;
	}

	unsigned int apps::GetEarliestPurchaseUnixTime(unsigned int nAppID)
	{
		return 0;
	}

	bool apps::BIsSubscribedFromFreeWeekend()
	{
		return false;
	}

	int apps::GetDLCCount()
	{
		return 0;
	}

	bool apps::BGetDLCDataByIndex(int iDLC, unsigned int* pAppID, bool* pbAvailable, char* pchName,
	                              int cchNameBufferSize)
	{
		return false;
	}

	void apps::InstallDLC(unsigned int nAppID)
	{
	}

	void apps::UninstallDLC(unsigned int nAppID)
	{
	}

	void apps::RequestAppProofOfPurchaseKey(unsigned int nAppID)
	{
	}

	bool apps::GetCurrentBetaName(char* pchName, int cchNameBufferSize)
	{
		strncpy_s(pchName, cchNameBufferSize, "public", cchNameBufferSize);
		return true;
	}

	bool apps::MarkContentCorrupt(bool bMissingFilesOnly)
	{
		return false;
	}

	unsigned int apps::GetInstalledDepots(int* pvecDepots, unsigned int cMaxDepots)
	{
		return 0;
	}

	unsigned int apps::GetAppInstallDir(unsigned int appID, char* pchFolder, unsigned int cchFolderBufferSize)
	{
		if (!pchFolder || !cchFolderBufferSize)
		{
			return 0;
		}

		pchFolder[0] = '\0';
		const auto mode = get_app_mode(appID);
		if (!mode)
		{
			return 0;
		}

		const auto directory = game::environment::get_binary_path(*mode).parent_path().string();
		strncpy_s(pchFolder, cchFolderBufferSize, directory.c_str(), _TRUNCATE);
		return static_cast<unsigned int>(std::strlen(pchFolder));
	}

	bool apps::BIsAppInstalled(unsigned int appID)
	{
		const auto mode = get_app_mode(appID);
		return mode && game::environment::is_mode_installed(*mode);
	}

	steam_id apps::GetAppOwner()
	{
		return SteamUser()->GetSteamID();
	}

	const char* apps::GetLaunchQueryParam(const char* pchKey)
	{
		return "";
	}

	bool apps::GetDlcDownloadProgress(uint32_t nAppID, uint64_t* punBytesDownloaded, uint64_t* punBytesTotal)
	{
		*punBytesTotal = 0;
		*punBytesDownloaded = 0;
		return false;
	}

	int apps::GetAppBuildId()
	{
		return 0;
	}

	void apps::RequestAllProofOfPurchaseKeys()
	{
	}

	uint64_t apps::GetFileDetails(const char* pszFileName)
	{
		return 0;
	}

	int apps::GetLaunchCommandLine(char* pszCommandLine, int cubCommandLine)
	{
		return 0;
	}

	bool apps::BIsSubscribedFromFamilySharing()
	{
		return false;
	}
}
