#include <std_include.hpp>
#include "../steam.hpp"
#include "component/name.hpp"
#include "component/steam_proxy.hpp"

namespace steam
{
	const char* friends::GetPersonaName()
	{
		return name::get_player_name();
	}

	unsigned long long friends::SetPersonaName(const char* pchPersonaName)
	{
		return 0;
	}

	int friends::GetPersonaState()
	{
		const auto client = steam_proxy::access_client();
		return client.friends ? client.friends->GetPersonaState() : 1;
	}

	int friends::GetFriendCount(int eFriendFlags)
	{
		const auto client = steam_proxy::access_client();
		return client.friends ? std::max(0, client.friends->GetFriendCount(eFriendFlags)) : 0;
	}

	steam_id* friends::GetFriendByIndex(steam_id* result, int iFriend, int iFriendFlags)
	{
		const auto client = steam_proxy::access_client();
		*result = {};

		if (client.friends && iFriend >= 0)
		{
			client.friends->GetFriendByIndex(result, iFriend, iFriendFlags);
			steam_proxy::remember_friend(result->bits);
		}

		return result;
	}

	int friends::GetFriendRelationship(steam_id steamIDFriend)
	{
		const auto client = steam_proxy::access_client();
		return client.friends ? client.friends->GetFriendRelationship(steamIDFriend) : 0;
	}

	int friends::GetFriendPersonaState(steam_id steamIDFriend)
	{
		const auto client = steam_proxy::access_client();
		return client.friends ? client.friends->GetFriendPersonaState(steamIDFriend) : 0;
	}

	const char* friends::GetFriendPersonaName(steam_id steamIDFriend)
	{
		const auto client = steam_proxy::access_client();
		static thread_local std::string name;
		name = client.friends ? client.friends->GetFriendPersonaName(steamIDFriend) : "";
		return name.c_str();
	}

	bool friends::GetFriendGamePlayed(steam_id steamIDFriend, friend_game_info* pFriendGameInfo)
	{
		if (!pFriendGameInfo)
		{
			return false;
		}

		const auto client = steam_proxy::access_client();
		*pFriendGameInfo = {};
		friend_game_info info{};

		if (!client.friends || !client.friends->GetFriendGamePlayed(steamIDFriend, &info))
		{
			return false;
		}

		// Show the game being played without offering to join a retail Steam lobby.
		pFriendGameInfo->game_id = info.game_id;
		return true;
	}

	const char* friends::GetFriendPersonaNameHistory(steam_id steamIDFriend, int iPersonaName)
	{
		const auto client = steam_proxy::access_client();
		static thread_local std::string name;
		name = client.friends ? client.friends->GetFriendPersonaNameHistory(steamIDFriend, iPersonaName) : "";
		return name.c_str();
	}

	int friends::GetFriendSteamLevel(steam_id steamIDFriend)
	{
		const auto client = steam_proxy::access_client();
		return client.friends ? client.friends->GetFriendSteamLevel(steamIDFriend) : 0;
	}

	const char* friends::GetPlayerNickname(steam_id steamIDPlayer)
	{
		const auto client = steam_proxy::access_client();
		const auto* nickname = client.friends ? client.friends->GetPlayerNickname(steamIDPlayer) : nullptr;
		static thread_local std::string name;
		name = nickname ? nickname : "";
		return name.empty() ? nullptr : name.c_str();
	}

	int friends::GetFriendsGroupCount()
	{
		return 0;
	}

	int16_t friends::GetFriendsGroupIDByIndex(int iFG)
	{
		return -1;
	}

	const char* friends::GetFriendsGroupName(int16_t friendsGroupID)
	{
		return nullptr;
	}

	int friends::GetFriendsGroupMembersCount(int16_t friendsGroupID)
	{
		return 0;
	}

	void friends::GetFriendsGroupMembersList(int16_t friendsGroupID, steam_id* pOutSteamIDMembers, int nMembersCount)
	{
		if (pOutSteamIDMembers && nMembersCount > 0)
		{
			std::fill_n(pOutSteamIDMembers, nMembersCount, steam_id{});
		}
	}

	bool friends::HasFriend(steam_id steamIDFriend, int eFriendFlags)
	{
		const auto client = steam_proxy::access_client();
		const auto is_friend = client.friends && client.friends->HasFriend(steamIDFriend, eFriendFlags);

		if (is_friend)
		{
			steam_proxy::remember_friend(steamIDFriend.bits);
		}

		return is_friend;
	}

	int friends::GetClanCount()
	{
		return 0;
	}

	steam_id* friends::GetClanByIndex(steam_id* result, int iClan)
	{
		*result = {};
		return result;
	}

	const char* friends::GetClanName(steam_id steamIDClan)
	{
		return "3arc";
	}

	const char* friends::GetClanTag(steam_id steamIDClan)
	{
		return this->GetClanName(steamIDClan);
	}

	bool friends::GetClanActivityCounts(steam_id steamID, int* pnOnline, int* pnInGame, int* pnChatting)
	{
		return false;
	}

	unsigned long long friends::DownloadClanActivityCounts(steam_id groupIDs[], int nIds)
	{
		return 0;
	}

	int friends::GetFriendCountFromSource(steam_id steamIDSource)
	{
		return 0;
	}

	steam_id* friends::GetFriendFromSourceByIndex(steam_id* result, steam_id steamIDSource, int iFriend)
	{
		*result = {};
		return result;
	}

	bool friends::IsUserInSource(steam_id steamIDUser, steam_id steamIDSource)
	{
		return false;
	}

	void friends::SetInGameVoiceSpeaking(steam_id steamIDUser, bool bSpeaking)
	{
	}

	void friends::ActivateGameOverlay(const char* pchDialog)
	{
	}

	void friends::ActivateGameOverlayToUser(const char* pchDialog, steam_id steamID)
	{
	}

	void friends::ActivateGameOverlayToWebPage(const char* pchURL)
	{
	}

	void friends::ActivateGameOverlayToStore(unsigned int nAppID, unsigned int eFlag)
	{
	}

	void friends::SetPlayedWith(steam_id steamIDUserPlayedWith)
	{
	}

	void friends::ActivateGameOverlayInviteDialog(steam_id steamIDLobby)
	{
	}

	int friends::GetSmallFriendAvatar(steam_id steamIDFriend)
	{
		const auto client = steam_proxy::access_client();
		return client.friends ? client.friends->GetSmallFriendAvatar(steamIDFriend) : 0;
	}

	int friends::GetMediumFriendAvatar(steam_id steamIDFriend)
	{
		const auto client = steam_proxy::access_client();
		return client.friends ? client.friends->GetMediumFriendAvatar(steamIDFriend) : 0;
	}

	int friends::GetLargeFriendAvatar(steam_id steamIDFriend)
	{
		const auto client = steam_proxy::access_client();
		return client.friends ? client.friends->GetLargeFriendAvatar(steamIDFriend) : 0;
	}

	bool friends::RequestUserInformation(steam_id steamIDUser, bool bRequireNameOnly)
	{
		const auto client = steam_proxy::access_client();
		return client.friends && client.friends->RequestUserInformation(steamIDUser, bRequireNameOnly);
	}

	unsigned long long friends::RequestClanOfficerList(steam_id steamIDClan)
	{
		return 0;
	}

	steam_id* friends::GetClanOwner(steam_id* result, steam_id steamIDClan)
	{
		*result = {};
		return result;
	}

	int friends::GetClanOfficerCount(steam_id steamIDClan)
	{
		return 0;
	}

	steam_id* friends::GetClanOfficerByIndex(steam_id* result, steam_id steamIDClan, int iOfficer)
	{
		*result = {};
		return result;
	}

	int friends::GetUserRestrictions()
	{
		return 0;
	}

	bool friends::SetRichPresence(const char* pchKey, const char* pchValue)
	{
		return true;
	}

	void friends::ClearRichPresence()
	{
	}

	const char* friends::GetFriendRichPresence(steam_id steamIDFriend, const char* pchKey)
	{
		const auto client = steam_proxy::access_client();
		static thread_local std::string presence;
		presence = client.friends && pchKey ? client.friends->GetFriendRichPresence(steamIDFriend, pchKey) : "";
		return presence.c_str();
	}

	int friends::GetFriendRichPresenceKeyCount(steam_id steamIDFriend)
	{
		const auto client = steam_proxy::access_client();
		return client.friends ? client.friends->GetFriendRichPresenceKeyCount(steamIDFriend) : 0;
	}

	const char* friends::GetFriendRichPresenceKeyByIndex(steam_id steamIDFriend, int iKey)
	{
		const auto client = steam_proxy::access_client();
		static thread_local std::string key;
		key = client.friends ? client.friends->GetFriendRichPresenceKeyByIndex(steamIDFriend, iKey) : "";
		return key.c_str();
	}

	void friends::RequestFriendRichPresence(steam_id steamIDFriend)
	{
		const auto client = steam_proxy::access_client();

		if (client.friends)
		{
			client.friends->RequestFriendRichPresence(steamIDFriend);
		}
	}

	bool friends::InviteUserToGame(steam_id steamIDFriend, const char* pchConnectString)
	{
		return false;
	}

	int friends::GetCoplayFriendCount()
	{
		return 0;
	}

	steam_id* friends::GetCoplayFriend(steam_id* result, int iCoplayFriend)
	{
		*result = {};
		return result;
	}

	int friends::GetFriendCoplayTime(steam_id steamIDFriend)
	{
		return 0;
	}

	unsigned int friends::GetFriendCoplayGame(steam_id steamIDFriend)
	{
		return 0;
	}

	unsigned long long friends::JoinClanChatRoom(steam_id steamIDClan)
	{
		return 0;
	}

	bool friends::LeaveClanChatRoom(steam_id steamIDClan)
	{
		return false;
	}

	int friends::GetClanChatMemberCount(steam_id steamIDClan)
	{
		return 0;
	}

	steam_id* friends::GetChatMemberByIndex(steam_id* result, steam_id steamIDClan, int iUser)
	{
		*result = {};
		return result;
	}

	bool friends::SendClanChatMessage(steam_id steamIDClanChat, const char* pchText)
	{
		return false;
	}

	int friends::GetClanChatMessage(steam_id steamIDClanChat, int iMessage, void* prgchText, int cchTextMax,
	                                unsigned int* peChatEntryType, steam_id* pSteamIDChatter)
	{
		return 0;
	}

	bool friends::IsClanChatAdmin(steam_id steamIDClanChat, steam_id steamIDUser)
	{
		return false;
	}

	bool friends::IsClanChatWindowOpenInSteam(steam_id steamIDClanChat)
	{
		return false;
	}

	bool friends::OpenClanChatWindowInSteam(steam_id steamIDClanChat)
	{
		return false;
	}

	bool friends::CloseClanChatWindowInSteam(steam_id steamIDClanChat)
	{
		return false;
	}

	bool friends::SetListenForFriendsMessages(bool bInterceptEnabled)
	{
		return false;
	}

	bool friends::ReplyToFriendMessage(steam_id steamIDFriend, const char* pchMsgToSend)
	{
		return false;
	}

	int friends::GetFriendMessage(steam_id steamIDFriend, int iMessageID, void* pvData, int cubData,
	                              unsigned int* peChatEntryType)
	{
		return 0;
	}

	unsigned long long friends::GetFollowerCount(steam_id steamID)
	{
		return 0;
	}

	unsigned long long friends::IsFollowing(steam_id steamID)
	{
		return 0;
	}

	unsigned long long friends::EnumerateFollowingList(unsigned int unStartIndex)
	{
		return 0;
	}
}
