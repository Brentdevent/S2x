#pragma once

#include <cstddef>
#include <cstdint>

namespace steam
{
	// CSteamID and CGameID are packed to one byte in the SDK. The other fields
	// give these structures four-byte alignment, even on Windows x64.
#pragma pack(push, 4)
	struct friend_game_info
	{
		uint64_t game_id;
		uint32_t ip;
		uint16_t game_port;
		uint16_t query_port;
		uint64_t lobby_id;
	};

	static_assert(sizeof(friend_game_info) == 24);
	static_assert(alignof(friend_game_info) == 4);
	static_assert(offsetof(friend_game_info, ip) == 8);
	static_assert(offsetof(friend_game_info, game_port) == 12);
	static_assert(offsetof(friend_game_info, query_port) == 14);
	static_assert(offsetof(friend_game_info, lobby_id) == 16);

	struct avatar_image_loaded
	{
		static constexpr int callback_id = 334;
		uint64_t steam_id;
		int image;
		int width;
		int height;
	};

	static_assert(sizeof(avatar_image_loaded) == 20);
	static_assert(offsetof(avatar_image_loaded, image) == 8);
	static_assert(offsetof(avatar_image_loaded, width) == 12);
	static_assert(offsetof(avatar_image_loaded, height) == 16);

	struct friend_rich_presence_update
	{
		static constexpr int callback_id = 336;
		uint64_t steam_id;
		uint32_t app_id;
	};

	static_assert(sizeof(friend_rich_presence_update) == 12);
	static_assert(offsetof(friend_rich_presence_update, app_id) == 8);
#pragma pack(pop)

	struct persona_state_change
	{
		static constexpr int callback_id = 304;
		uint64_t steam_id;
		int change_flags;
	};

	static_assert(sizeof(persona_state_change) == 16);
	static_assert(offsetof(persona_state_change, change_flags) == 8);

	// SteamFriends015. CSteamID returns use an explicit x64 return buffer:
	// the SDK's CSteamID has a constructor, while our steam_id is a POD union.
	class friends
	{
	public:
		~friends() = default;

		virtual const char* GetPersonaName();
		virtual unsigned long long SetPersonaName(const char* pchPersonaName);
		virtual int GetPersonaState();
		virtual int GetFriendCount(int eFriendFlags);
		virtual steam_id* GetFriendByIndex(steam_id* result, int iFriend, int iFriendFlags);
		virtual int GetFriendRelationship(steam_id steamIDFriend);
		virtual int GetFriendPersonaState(steam_id steamIDFriend);
		virtual const char* GetFriendPersonaName(steam_id steamIDFriend);
		virtual bool GetFriendGamePlayed(steam_id steamIDFriend, friend_game_info* pFriendGameInfo);
		virtual const char* GetFriendPersonaNameHistory(steam_id steamIDFriend, int iPersonaName);
		virtual int GetFriendSteamLevel(steam_id steamIDFriend);
		virtual const char* GetPlayerNickname(steam_id steamIDPlayer);
		virtual int GetFriendsGroupCount();
		virtual int16_t GetFriendsGroupIDByIndex(int iFG);
		virtual const char* GetFriendsGroupName(int16_t friendsGroupID);
		virtual int GetFriendsGroupMembersCount(int16_t friendsGroupID);
		virtual void GetFriendsGroupMembersList(int16_t friendsGroupID, steam_id* pOutSteamIDMembers, int nMembersCount);
		virtual bool HasFriend(steam_id steamIDFriend, int eFriendFlags);
		virtual int GetClanCount();
		virtual steam_id* GetClanByIndex(steam_id* result, int iClan);
		virtual const char* GetClanName(steam_id steamIDClan);
		virtual const char* GetClanTag(steam_id steamIDClan);
		virtual bool GetClanActivityCounts(steam_id steamID, int* pnOnline, int* pnInGame, int* pnChatting);
		virtual unsigned long long DownloadClanActivityCounts(steam_id groupIDs[], int nIds);
		virtual int GetFriendCountFromSource(steam_id steamIDSource);
		virtual steam_id* GetFriendFromSourceByIndex(steam_id* result, steam_id steamIDSource, int iFriend);
		virtual bool IsUserInSource(steam_id steamIDUser, steam_id steamIDSource);
		virtual void SetInGameVoiceSpeaking(steam_id steamIDUser, bool bSpeaking);
		virtual void ActivateGameOverlay(const char* pchDialog);
		virtual void ActivateGameOverlayToUser(const char* pchDialog, steam_id steamID);
		virtual void ActivateGameOverlayToWebPage(const char* pchURL);
		virtual void ActivateGameOverlayToStore(unsigned int nAppID, unsigned int eFlag);
		virtual void SetPlayedWith(steam_id steamIDUserPlayedWith);
		virtual void ActivateGameOverlayInviteDialog(steam_id steamIDLobby);
		virtual int GetSmallFriendAvatar(steam_id steamIDFriend);
		virtual int GetMediumFriendAvatar(steam_id steamIDFriend);
		virtual int GetLargeFriendAvatar(steam_id steamIDFriend);
		virtual bool RequestUserInformation(steam_id steamIDUser, bool bRequireNameOnly);
		virtual unsigned long long RequestClanOfficerList(steam_id steamIDClan);
		virtual steam_id* GetClanOwner(steam_id* result, steam_id steamIDClan);
		virtual int GetClanOfficerCount(steam_id steamIDClan);
		virtual steam_id* GetClanOfficerByIndex(steam_id* result, steam_id steamIDClan, int iOfficer);
		virtual int GetUserRestrictions();
		virtual bool SetRichPresence(const char* pchKey, const char* pchValue);
		virtual void ClearRichPresence();
		virtual const char* GetFriendRichPresence(steam_id steamIDFriend, const char* pchKey);
		virtual int GetFriendRichPresenceKeyCount(steam_id steamIDFriend);
		virtual const char* GetFriendRichPresenceKeyByIndex(steam_id steamIDFriend, int iKey);
		virtual void RequestFriendRichPresence(steam_id steamIDFriend);
		virtual bool InviteUserToGame(steam_id steamIDFriend, const char* pchConnectString);
		virtual int GetCoplayFriendCount();
		virtual steam_id* GetCoplayFriend(steam_id* result, int iCoplayFriend);
		virtual int GetFriendCoplayTime(steam_id steamIDFriend);
		virtual unsigned int GetFriendCoplayGame(steam_id steamIDFriend);
		virtual unsigned long long JoinClanChatRoom(steam_id steamIDClan);
		virtual bool LeaveClanChatRoom(steam_id steamIDClan);
		virtual int GetClanChatMemberCount(steam_id steamIDClan);
		virtual steam_id* GetChatMemberByIndex(steam_id* result, steam_id steamIDClan, int iUser);
		virtual bool SendClanChatMessage(steam_id steamIDClanChat, const char* pchText);
		virtual int GetClanChatMessage(steam_id steamIDClanChat, int iMessage, void* prgchText, int cchTextMax,
		                               unsigned int* peChatEntryType, steam_id* pSteamIDChatter);
		virtual bool IsClanChatAdmin(steam_id steamIDClanChat, steam_id steamIDUser);
		virtual bool IsClanChatWindowOpenInSteam(steam_id steamIDClanChat);
		virtual bool OpenClanChatWindowInSteam(steam_id steamIDClanChat);
		virtual bool CloseClanChatWindowInSteam(steam_id steamIDClanChat);
		virtual bool SetListenForFriendsMessages(bool bInterceptEnabled);
		virtual bool ReplyToFriendMessage(steam_id steamIDFriend, const char* pchMsgToSend);
		virtual int GetFriendMessage(steam_id steamIDFriend, int iMessageID, void* pvData, int cubData,
		                             unsigned int* peChatEntryType);
		virtual unsigned long long GetFollowerCount(steam_id steamID);
		virtual unsigned long long IsFollowing(steam_id steamID);
		virtual unsigned long long EnumerateFollowingList(unsigned int unStartIndex);
	};
}
