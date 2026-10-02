#pragma once

#include "structs.hpp"
#include "loader/component_loader.hpp"

#define WEAK __declspec(selectany)

namespace game
{
	WEAK symbol<void(const char* text, int max_chars, const Font_s* font, int unk0, int unk1, int pixel_h, 
		float pos_x, float pos_y, float scale_x, float scale_y, float rotation, const float* color, const void* glowStyle)> R_AddCmdDrawText{ 0x8BA480, 0x81A3A0, 0x639430 };
	WEAK symbol<Font_s*(const char* name, int size)> R_RegisterFont{ 0x893D90, 0x7F3D40, 0x612EB0 };

	WEAK symbol<Material*(const char* material)> Material_RegisterHandle{ 0x8AC5F0, 0x80C500, 0x62B0E0 };

	WEAK symbol<void*(const char* text, int maxChars, Font_s* font, uint8_t unk0, uint8_t unk1, int fontHeight, float x, float y, float xScale, float yScale, 
		float rotation, const float* color, uint16_t styleFlags, float glowAlpha, int cursorPos, char cursor, const void* glowStyle)> AddBaseDrawTextCmd{ 0x8B80C0, 0x817FE0, 0x637510 };

	// 4th and 5th parameters are always 0, names are guessed.
	WEAK symbol<float(const char* text, int maxChars, Font_s* font, uint8_t iconFontIndex, uint8_t iconFontFlags)> R_TextWidth{ 0x894240, 0x7F41F0, 0x613360 };
	WEAK symbol<float(const char* text, int maxChars, Font_s* font, uint8_t iconFontIndex, uint8_t iconFontFlags,
		unsigned int pixelHeight, float tracking, int glyphFlags)> R_TextWidthWithFlags{ 0x8945F0, 0x7F45A0, 0x613720 };
	WEAK symbol<int(Font_s* font)> R_GetFontHeight{ 0x763500, 0x6EEB40, 0x4CC130 };
	WEAK symbol<void*(int style)> R_Font_GetLegacyFontStyle{ 0x893A90, 0x7F3A40, 0x612BB0 };
	WEAK symbol<void(float x, float y, float width, float height, float s0, float t0, float s1, float t1,
		const float* color, Material* material)> R_AddCmdDrawStretchPic{ 0x8B9A00, 0x819920, 0x638AB0 };

#define R_AddCmdDrawTextWithCursor(TXT, MC, F, X, Y, XS, YS, R, C, S, CP, CC) \
	game::AddBaseDrawTextCmd( \
		TXT, MC, F, \
		0, 0, game::R_GetFontHeight(F), \
		X, Y, XS, YS, R, C, \
		static_cast<uint16_t>(S), \
		0.0f, \
		CP, CC, \
		nullptr \
	)
	
	WEAK symbol<void()> Com_InitDvars{ 0x93260, 0x76720 };
	WEAK symbol<void()> Com_Quit_f{ 0x9A130, 0x7A0E0, 0x471D30 };
	WEAK symbol<void()> j_Com_Quit_f{ 0x78A5E0, 0x715050, 0x4E9AF0 };
	WEAK symbol<void()> SV_FastRestart_f{ 0x6D6BA0, 0x662170, 0x5885D0 };
	WEAK symbol<void()> Cbuf_AddServerText_f{ 0x64A1A0, 0x5D5900, 0x464E60 };

	WEAK symbol<void(const int critical_section)> RtlEnterCriticalSection{ 0x7729B0, 0x6FE750, 0x4DA170 };
	WEAK symbol<void(const int critical_section)> RtlLeaveCriticalSection{ 0x772A20, 0x6FE7C0, 0x4DA1E0 };

	WEAK symbol<dvar_t*(const char* dvarName, bool value, DvarFlags flags)> Dvar_RegisterBool{ 0xB05E0, 0x90410, 0x4CE830 };
	WEAK symbol<dvar_t*(const char* dvarName, float value, float min, float max, DvarFlags flags)> Dvar_RegisterFloat{ 0xB0A50, 0x90880, 0x4CEA90 };
	WEAK symbol<dvar_t*(const char* dvarName, int value, int min, int max, DvarFlags flags)> Dvar_RegisterInt{ 0xB0C70, 0x90AA0, 0x4CEB00 };
	WEAK symbol<dvar_t*(const char* dvarName, const char* value, DvarFlags flags)> Dvar_RegisterString{ 0xB1460, 0x91290, 0x4CEDF0 };
	WEAK symbol<dvar_t*(const char* dvarName, DvarType type, DvarFlags flags, DvarValue* value, DvarLimits* domain)> Dvar_RegisterVariant{ 0, 0x4CEE30 };
	WEAK symbol<dvar_t*(const char* dvarName, const char* const* valueList, int defaultIndex, DvarFlags flags)> Dvar_RegisterEnum{ 0xB0970, 0x907A0, 0x4CEA20 };
	WEAK symbol<dvar_t*(const char* dvarName, float x, float y, float min, float max, DvarFlags flags)> Dvar_RegisterVec2{ 0xB1520, 0x91350, 0x4CF000 };
	WEAK symbol<dvar_t*(const char* dvarName, float x, float y, float z, float min, float max, DvarFlags flags)> Dvar_RegisterVec3{ 0xB1620, 0x91450, 0x4CF100 };
	WEAK symbol<dvar_t*(const char* dvarName, float x, float y, float z, float w, float min, float max, DvarFlags flags)> Dvar_RegisterVec4{ 0xB1860, 0x91690, 0x4CF2A0 };
	WEAK symbol<dvar_t*(const char* dvarName, float x, float y, float z, float max, DvarFlags flags)> Dvar_RegisterVec3Color{ 0xB1760, 0x91590, 0x4CF220 };

	WEAK symbol<dvar_t*(const char* dvarName, bool value, DvarFlags flags)> Dvar_RegisterBoolProtected{ 0xB06A0, 0x904D0 };
	WEAK symbol<dvar_t*(const char* dvarName, float value, float min, float max, DvarFlags flags)> Dvar_RegisterFloatProtected{ 0xB0B60, 0x90990 };
	WEAK symbol<dvar_t*(const char* dvarName, int value, int min, int max, DvarFlags flags)> Dvar_RegisterIntProtected{ 0xB0D40, 0x90B70 };

	WEAK symbol<dvar_t*(const char* dvarName)> Dvar_FindMalleableVar{ 0xAF8E0, 0x8F710, 0x4CDBE0 };
	WEAK symbol<void (dvar_t* dvar, DvarFlags flags)> Dvar_SetFlags{ 0xAF370, 0x8F1A0 };
	WEAK symbol<int()> Dvar_Command{ 0x664880, 0x5F0150 };

	WEAK symbol<bool(const char* dvarName)> Dvar_GetBool{ 0xAFA70, 0x8F8A0 };
	WEAK symbol<float(const char* dvarName)> Dvar_GetFloat{ 0xAFBC0, 0x8F9F0 };
	WEAK symbol<int(const char* dvarName)> Dvar_GetInt{ 0xAFCB0, 0x8FAE0 };
	WEAK symbol<const char*(const char* dvarName)> Dvar_GetString{ 0xAFD20, 0x8FB50 };
	WEAK symbol<int64_t(const char* dvarName)> Dvar_GetInt64{ 0xAFC10, 0x8FA40 };

	WEAK symbol<const char*(dvar_t* dvar, bool decode, DvarValue* dValue)> Dvar_ValueToString{ 0xB4520, 0x94350, 0x4D1230 };

	WEAK symbol<dvar_t*(const char* dvarName, bool value)> Dvar_SetBoolByName{ 0xB2020, 0x91E50 };
	WEAK symbol<dvar_t*(const char* dvarName, int value)> Dvar_SetIntByName{ 0xB2A40, 0x92870 };
	WEAK symbol<dvar_t*(const char* dvarName, float value)> Dvar_SetFloatByName{ 0xB2790, 0x925C0 };
	WEAK symbol<dvar_t*(const char* dvarName, const char* value)> Dvar_SetStringByName{ 0xB3070, 0x92EA0 };

	WEAK symbol<void(const char* dvarName, const char* string)> Dvar_SetCommand{ 0xB2360, 0x92190 };
	WEAK symbol<void(dvar_t* dvar, bool value)> Dvar_SetBool{ 0xB1FD0, 0x91E00 };
	WEAK symbol<void(dvar_t* dvar, float value)> Dvar_SetFloat{ 0xB2700, 0x92530 };
	WEAK symbol<void(dvar_t* dvar, int value)> Dvar_SetInt{ 0xB29C0, 0x927F0 };
	WEAK symbol<void(dvar_t* dvar, const char* value)> Dvar_SetString{ 0xB2E30, 0x92C60 };

	WEAK symbol<bool(int* min, int* max)> CG_GetFovRange{ 0x45830, 0x37F90, 0x23F6B0 };
	// MP only. Both getters decrypt their pointers and check their callers; use call_safe.
	WEAK symbol<const std::byte*(int localClientNum)> CG_GetLocalClient{ 0x15330, 0x3A90E0 };
	WEAK symbol<void(int localClientNum)> CG_ProcessSnapshots{ 0x43ADA0, 0x3C6390 };
	WEAK symbol<const std::byte*(int localClientNum)> CG_ReadNextSnapshot{ 0x43B580, 0x3C6B80 };

	WEAK symbol<const ScreenPlacement*(const LocalClientNum_t localClientNum)> ScrPlace_GetViewPlacement{ 0x4A01B0, 0x42B950, 0x272660 };

	WEAK symbol<const char*(int index)> Cmd_Argv{ 0x8D9F0, 0x70D40 };
	WEAK symbol<void(const char* text_in)> SV_Cmd_TokenizeString{ 0x64C070, 0x5D77D0, 0x4665A0 };
	WEAK symbol<void(const char* text_in)> Cmd_TokenizeString{ 0x64B970, 0x5D70D0, 0x465EA0 };
	WEAK symbol<void()> SV_Cmd_EndTokenizedString{ 0x64C030, 0x5D7790, 0x466560 };
	WEAK symbol<void()> Cmd_EndTokenizedString{ 0x64AA00, 0x5D6160, 0x465410 };
	WEAK symbol<void(const char* cmdName, void(__fastcall* function)(), cmd_function_s* allocedCmd)> Cmd_AddCommandInternal{ 0x64A6B0, 0x5D5E10, 0x465150 };
	WEAK symbol<void(const char* cmdName, void(__fastcall* function)(), cmd_function_s* allocedCmd)> Cmd_AddServerCommandInternal{ 0x64A720, 0x5D5E80, 0x4651C0 };
	WEAK symbol<unsigned int(const char* string)> DDL_HashString{ 0x8E900, 0x71C50 };
	WEAK symbol<unsigned int(const void* state)> DDL_GetType{ 0xA1CA40, 0xA449D0 };
	WEAK symbol<void(const void* definition, void* state, unsigned int statsGroup)> DDL_InitState{ 0x6546A0, 0x5DFDF0 };
	WEAK symbol<bool(const void* fromState, void* toState, int pathCount, const unsigned int* path)> DDL_MoveToPath{ 0xA1D1C0, 0xA45150 };
	WEAK symbol<bool(int controllerIndex)> LiveStorage_DoWeHaveStats{ 0xCECE0, 0xAE9B0 };
	WEAK symbol<const void*(unsigned int statsGroup)> LiveStorage_GetStatsGroupDDLDefinition{ 0x653FC0, 0x5DF710 };
	WEAK symbol<bool(int controllerIndex, const unsigned int* path, unsigned int pathCount, int value,
		unsigned int statsGroup)> LiveStorage_PlayerDataSetIntByNameArray{ 0x18E8B0, 0x16CD20 };
	WEAK symbol<void(int controllerIndex)> LiveStorage_StatsWriteNeeded{ 0xD4DB0, 0xB4820 };
	WEAK symbol<char*(const char* filename, char* buffer, int size)> DB_ReadRawFile{ 0xA7330, 0x871A0, 0x2AED30 };
	WEAK symbol<void(const char* name)> Com_BeginParseSession{ 0x76A490, 0x6F6230 };
	WEAK symbol<void()> Com_EndParseSession{ 0x76A590, 0x6F6330 };
	WEAK symbol<const char*(const char** cursor)> Com_Parse{ 0x76A950, 0x6F66F0 };
	WEAK symbol<bool(const char* name, int fileType)> DB_FastfileExists{ 0xA0E50, 0x80CC0 };
	WEAK symbol<bool(int pack)> Content_IsPackAvailable{ 0x766160, 0x6F1970 };
	WEAK symbol<std::int64_t(const char* filename, char** buffer)> FS_ReadFile{ 0x7583C0, 0x6E3A20, 0x4C0E60 };
	WEAK symbol<void(void* buffer)> FS_FreeFile{ 0x7583B0, 0x6E3A10, 0x4C0E50 };
	WEAK symbol<void(const char* gameName)> FS_Startup{ 0x757330, 0x6E2990, 0x4BFDF0 };
	WEAK symbol<void(const char* path, const char* dir)> FS_AddLocalizedGameDirectory{ 0x755850, 0x6E0EC0, 0x4BE590 };
	WEAK symbol<void(int localClientNum, const char* map, bool mapIsPreloaded)> SV_StartMap{ 0x6D8200, 0x663810 };
	WEAK symbol<void(const char* reason)> SV_Shutdown{ 0x6DBF50, 0x667590 };
	WEAK symbol<bool()> SV_Loaded{ 0x6DB810, 0x666E50, 0x58A2C0 };
	WEAK symbol<void(mp::client_t* client, svscmd_type type, const char* format, ...)> SV_SendServerCommand{ 0x6E0BA0, 0x66C160 };
	WEAK symbol<void(int localClientNum, const char* text)> CL_ForwardCommandToServer{ 0x75060, 0x5E990 };
	WEAK symbol<void(unsigned int localClientNum)> CG_DeployServerCommandString{ 0x431EB0, 0x3BD740 };

	WEAK symbol<void(XAssetType type, void(*callback)(XAssetHeader, void*), void* data,
		bool includeOverride)> DB_EnumXAssets_FastFile{ 0xA0B00, 0x80970, 0x2AAFF0 };
	WEAK symbol<const char*(const XAsset* asset)> DB_GetXAssetName{ 0x4A3EF0, 0x42F630, 0x2764F0 };
	WEAK symbol<const char*(XAssetType type)> DB_GetXAssetTypeName{ 0x4A3F10, 0x42F650, 0x276510 };
	WEAK symbol<void(XZoneInfo* zoneInfo, unsigned int zoneCount, DBSyncMode syncMode)> DB_LoadXAssets{ 0xA4F60, 0x84DD0, 0x2ADB50 };
	WEAK symbol<void(const char* zoneName, int zoneFlags, int isBaseMap)> DB_TryLoadXFileInternal{ 0xACE30, 0x8CC50, 0x2AF980 };
	WEAK symbol<void(uint16_t* zoneIndices, unsigned int zoneCount, bool createDefault)> DB_UnloadXZones{ 0xADE50, 0x8DC70, 0x2B05D0 };
	WEAK symbol<XAssetHeader(XAssetType type, const char* name, int allowCreateDefault)> DB_FindXAssetHeader{ 0xA1080, 0x80EF0, 0x2AB340 };
	WEAK symbol<void()> DB_SyncXAsset{ 0xACA60, 0x8C8D0 };
	WEAK symbol<int(XAssetType type, const char* name)> DB_XAssetExists{ 0xAE400, 0x8E220, 0x2B0870 };
	WEAK symbol<int(XAssetType type, const char* name)> DB_IsXAssetDefault{ 0xA3C90, 0x83B00, 0x2ACEB0 };
	WEAK symbol<int(const RawFile* rawfile)> DB_GetRawFileLen{ 0xA27C0, 0x82630, 0x2AC1D0 };
	WEAK symbol<void(const RawFile* rawfile, char* buf, int size)> DB_GetRawBuffer{ 0xA2650, 0x824C0, 0x2AC060 };

	WEAK symbol<void(const char* mapName, const char* gameType)> CL_PreloadMap{ 0x83950, 0x67090 };
	WEAK symbol<void(const char* mapName)> CL_PreloadMap2{ 0x837E0, 0x66F20 };
	WEAK symbol<void(unsigned int localClientNum)> CL_CheckForResend{ 0x6AF20, 0x548A0 };
	WEAK symbol<bool(int localClientNum)> CL_IsLocalClientInGame{ 0x7E110, 0x62FB0 };
	// Copy cg->refdef.view.org / axis[0]; the native wrappers handle CG_GetLocalClient's caller check.
	WEAK symbol<void(int localClientNum, float* pos)> CL_GetViewPos{ 0x7B8B0, 0x60720 };
	WEAK symbol<void(int localClientNum, float* forward)> CL_GetViewForward{ 0x7B880, 0x606E0 };
	WEAK symbol<void(int a, int b)> CL_VirtualLobbyShutdown{ 0x8BB80, 0x6F140 };
	WEAK symbol<bool(int localClientNum, netadr_s* from, msg_t* msg, int time)> CL_DispatchConnectionlessPacket{ 0x6F7E0, 0x59040 };
	WEAK symbol<void(int localClientNum, void* sessionInfo, netadr_s* to, const char* mapname, const char* gametype)> CL_ConnectAndPreloadMap{ 0x6CCA0, 0x565B0 };
	WEAK symbol<void()> CL_Connect{ 0x6D0A0, 0x569D0 };
	WEAK symbol<void()> CL_Disconnect_f{ 0x6F7A0, 0x59000 };
	WEAK symbol<void()> CL_Live_StartPrivateMatchHost{ 0x80290, 0x65100 };
	WEAK symbol<int(int localClientNum)> CL_ControllerIndexFromClientNum{ 0x4A0B80, 0x42C320 };
	WEAK symbol<char*(int controllerIndex)> CL_GetUsernameForLocalClient{ 0x789680, 0x713FF0 };
	WEAK symbol<bool(int controllerIndex, char* name, std::size_t nameSize)> Live_GetLocalClientName{ 0x7896E0, 0x714050 };

	WEAK symbol<void(uint16_t ent, const char* menu)> CG_RegisterHubVendorTarget{ 0x2ED20, 0x22930 };

	WEAK symbol<void(int localClientNum, const char** args)> UI_RunMenuScript{ 0x746A50, 0x6D2330 };
	WEAK symbol<void(int localClientNum)> UI_StartServer{ 0x74A440, 0x6D5D20 };
	WEAK symbol<void()> GameInfo_UpdateArenas{ 0x650A70, 0x5DC1C0 };
	WEAK symbol<int(const char* mapName)> GameInfo_GetIndexForMapName{ 0x650490, 0x5DBBE0 };
	WEAK symbol<const char*(const char* mapName)> UI_GetMapDisplayName{ 0x6505A0, 0x5DBCF0 };
	WEAK symbol<const char*(const char* gameType)> UI_GetCustomizedGameTypeDefaultDisplayName{ 0x650160, 0x5DB8B0 };
	WEAK symbol<int(const char* mapName)> UI_GetListIndexFromMapName{ 0x650510, 0x5DBC60 };
	WEAK symbol<void(const char* mapname, const char* gametype)> UI_SetMap{ 0x74A050, 0x6D58C0 };
	WEAK symbol<void(int localClientNum)> UI_Map{ 0x744C30, 0x6D0510 };

	WEAK symbol<uint16_t(char* dst, const char* src, int length)> Sys_ChecksumCopy{ 0x75FB80, 0x6EB1C0 };
	WEAK symbol<int(netsrc_t sock, int length, const void* data, const netadr_s* to)> Sys_SendPacket{ 0x7B0F50, 0x717420 };
	WEAK symbol<int(netsrc_t sock, int length, const void* data, const netadr_s* to)> NET_SendPacket{ 0x66E290, 0x5F98B0 };
	WEAK symbol<int(const netadr_s* a, const netadr_s* b)> NET_CompareAdr{ 0x66DAE0, 0x5F9120 };
	WEAK symbol<int(const netadr_s* a, const netadr_s* b)> NET_CompareBaseAdr{ 0x66DB50, 0x5F9190 };
	WEAK symbol<int(const char* address, netadr_s* out)> NET_StringToAdr{ 0x66E3E0, 0x5F9A00 };
	WEAK symbol<void(const netadr_s* address, sockaddr* s)> NetadrToSockadr{ 0x75F9E0, 0x6EB020 };

	WEAK symbol<PartyData*(int unk)> Lobby_GetPartyData{ 0x47D050, 0x4090D0 };
	WEAK symbol<void*(int localClientNum)> Lobby_GetLocalClientData{ 0x470D30, 0x3FD020 };
	WEAK symbol<PartyData*(void* localClientData)> Lobby_GetPartyDataFromLocalClient{ 0x470F20, 0x3FD210 };
	WEAK symbol<PartyData*()> Party_GetPrivatePartyData{ 0x47E350, 0x40A3D0 };
	WEAK symbol<PartyData*()> Live_GetGameParty{ 0x789620, 0x713F80 };
	WEAK symbol<std::uint8_t(PartyData* partyData, std::uint64_t xuid)> Party_FindMemberByXUID{ 0x6FDDA0, 0x689310 };
	WEAK symbol<int(PartyData* partyData)> Party_IsRunning{ 0x47A6C0, 0x4067D0 };
	WEAK symbol<int(PartyData* partyData)> Party_AreWeHost{ 0x47A6F0, 0x406800 };
	WEAK symbol<bool(PartyData* partyData, int memberIndex)> Party_IsHost{ 0x480F10, 0x40CF30 };
	WEAK symbol<int(PartyData* partyData, int memberIndex)> Party_IsMemberLocalPlayer{ 0x481320, 0x40D340 };
	WEAK symbol<int(PartyData* partyData, int memberIndex)> Party_IsMemberUIVisible{ 0x481370, 0x40D390 };
	WEAK symbol<int(PartyData* partyData)> Party_IsWaitingForMembers{ 0x4819A0, 0x40D990 };
	WEAK symbol<int(PartyData* partyData)> Party_GetPublicMatch{ 0x197190, 0x173A70 };
	WEAK symbol<void(PartyData* partyData, int maxClients)> Party_SetMaxClients{ 0x1973D0, 0x173CB0 };
	WEAK symbol<void(PartyData* partyData, int minClients)> Party_SetMinClients{ 0x1973E0, 0x173CC0 };
	WEAK symbol<int(PartySettings* settings)> PartySettings_GetPrivateMatch{ 0x197140, 0x173A20 };
	WEAK symbol<int(PartySettings* settings)> PartySettings_GetRankedMatch{ 0x197180, 0x173A60 };
	WEAK symbol<void(PartySettings* settings, bool privateMatch)> PartySettings_SetPrivateMatch{ 0x1973F0, 0x173CD0 };
	WEAK symbol<void(PartySettings* settings, bool publicMatch)> PartySettings_SetPublicMatch{ 0x197440, 0x173D20 };
	WEAK symbol<void(PartySettings* settings, bool rankedMatch)> PartySettings_SetRankedMatch{ 0x197430, 0x173D10 };
	WEAK symbol<int()> Lobby_HowManyPlayersCanWeHost{ 0x663BF0, 0x5EF4C0 };
	WEAK symbol<void()> PartyHost_NotifyPrivateMatchCreated{ 0x12A2F0, 0x10AAE0 };
	WEAK symbol<std::int64_t(PartyData* partyData, std::uint8_t state)> PartyHost_SetState{ 0x494930, 0x420260 };
	WEAK symbol<void(PartyData* partyData, unsigned int localControllerIndex)> PartyHost_PreMatch{ 0x48D8C0, 0x419240 };
	WEAK symbol<void(PartyData* partyData, void* activeClient)> PartyHost_StartMatch{ 0x4917B0, 0x41D1A0 };
	WEAK symbol<std::int64_t(PartyData* partyData, void* activeClient)> PartyHost_AutoStart{ 0x491A80, 0x41D470 };
	WEAK symbol<std::int64_t(PartyData* partyData, void* commandData, netadr_s* from, msg_t* msg)> PartyClient_HandleGo{ 0x4728F0, 0x3FE5E0 };
	WEAK symbol<void(PartyData* partyData, std::uint32_t* activeClient, netadr_s* from)> PartyClient_ProcessPartyState{ 0x4777E0, 0x4039D0 };
	WEAK symbol<std::int64_t(int controllerIndex)> PartyClient_SetLocalReadyUpFlag{ 0x483CE0, 0x40FCD0 };
	WEAK symbol<const char*()> PartyHost_EndMatch{ 0x6DFEB0, 0x66B490 };
	WEAK symbol<const char*(PartyData* partyData)> Party_GetMapName{ 0x1970E0, 0x1739C0 };
	WEAK symbol<const char*(PartyData* partyData)> Party_GetGameType{ 0x1970A0, 0x173980 };
	WEAK symbol<void(PartyData* partyData, const char* gametype)> Party_SetGameType{ 0x1973A0, 0x173C80 };
	WEAK symbol<void(PartyData* partyData, const char* mapname)> Party_SetMapName{ 0x1973C0, 0x173CA0 };
	WEAK symbol<SessionData*(int sessionIndex)> Session_GetData{ 0x6FDE10, 0x689380 };
	WEAK symbol<void(const void* address, char* buffer)> Session_HostAddressToString{ 0x66EDD0, 0x5FA3D0 };
	WEAK symbol<void(const void* key, char* buffer)> Session_KeyToString{ 0x66D970, 0x5F8FB0 };
	WEAK symbol<void(std::uint64_t sessionId, char* buffer)> Session_IdToString{ 0x66D9B0, 0x5F8FF0 };

	WEAK symbol<bool()> BG_BotFastFileEnabled{ 0x3879A0, 0x3322D0 };
	WEAK symbol<bool()> BG_BotSystemEnabled{ 0x388180, 0x332AB0 };
	WEAK symbol<bool(void* unk)> BG_AgentSystemEnabled{ 0x387BC0, 0x3324F0 };
	WEAK symbol<bool()> BG_BotsUsingTeamDifficulty{ 0x388310, 0x332C40 };
	WEAK symbol<uint64_t()> BG_NetDataChecksum{ 0x38BD70, 0x3366B0 };

	WEAK symbol<unsigned int()> Scr_AllocArray{ 0x68BC40, 0x617210, 0x492480 };
	WEAK symbol<const float*(const float* v)> Scr_AllocVector{ 0x68BDB0, 0x617380, 0x492590 };
	WEAK symbol<const char*(unsigned int index)> Scr_GetString{ 0x690C00, 0x61C1D0, 0x497470 };
	WEAK symbol<void(const char* value)> Scr_AddString{ 0x68FC30, 0x61B200, 0x496360 };
	WEAK symbol<void(int value)> Scr_AddInt{ 0x68FB50, 0x61B120, 0x496280 };
	WEAK symbol<scr_string_t(unsigned int index)> Scr_GetConstString{ 0x690640, 0x61BC10, 0x496E40 };
	WEAK symbol<int(unsigned int index)> Scr_GetInt{ 0x690970, 0x61BF40, 0x4971E0 };
	WEAK symbol<float(unsigned int index)> Scr_GetFloat{ 0x690880, 0x61BE50, 0x497080 };
	WEAK symbol<void(unsigned int index, float* vectorValue)> Scr_GetVector{ 0x690DE0, 0x61C3B0, 0x497650 };
	WEAK symbol<unsigned int()> Scr_GetNumParam{ 0x6909E0, 0x61BFB0, 0x497250 };
	WEAK symbol<void()> Scr_ClearOutParams{ 0x690090, 0x61B660, 0x4967C0 };
	WEAK symbol<scr_entref_t(unsigned int entId)> Scr_GetEntityIdRef{ 0x68E560, 0x619B30, 0x494CA0 };
	WEAK symbol<int(unsigned int classnum, int entnum, int offset)> Scr_SetObjectField{ 0x5BA910, 0x546050, 0x3C9990 };
	WEAK symbol<void(void* ent, unsigned int stringValue, unsigned int paramcount)> Scr_Notify{ 0x5B9D90, 0x5454D0, 0x3C9770 };
	WEAK symbol<void(unsigned int id, unsigned int stringValue, unsigned int paramcount)> Scr_NotifyId{ 0x691490, 0x61CA60, 0x497D40 };
	WEAK symbol<bool(VariableValue* value)> Scr_CastString{ 0x68C050, 0x617620, 0x492830 };
	WEAK symbol<int(unsigned int index)> Scr_GetType{ 0x690D10, 0x61C2E0, 0x497580 };
	WEAK symbol<const char*(unsigned int index)> Scr_GetTypeName{ 0x690D50, 0x61C320, 0x4975C0 };
	WEAK symbol<int(unsigned int index)> Scr_GetPointerType{ 0x690B90, 0x61C160, 0x497400 };
	WEAK symbol<unsigned int(unsigned int index)> Scr_GetObject{ 0x6909F0, 0x61BFC0, 0x497260 };
	WEAK symbol<const char*(unsigned int index)> Scr_GetConstIString{ 0x690900, 0x61BED0, 0x497170 };
	WEAK symbol<void(int parmIndex, const char* token, int tokenLen)> Scr_ValidateLocalizedStringRef{ 0x5A04E0, 0x52BC20, 0x3B3CB0 };
	WEAK symbol<uint16_t(int handle, unsigned int paramcount)> Scr_ExecThread{ 0x6901F0, 0x61B7C0, 0x496930 };
	WEAK symbol<void(scr_string_t filename, unsigned int threadName, char* codePos)> Scr_EmitFunction{ 0x684DC0, 0x610390, 0x48B5E0 };
	WEAK symbol<unsigned int(const char* name)> Scr_LoadScript{ 0x685B10, 0x6110E0, 0x48C310 };
	WEAK symbol<unsigned int(const char* filename, unsigned int name)> Scr_GetFunctionHandle{ 0x6859A0, 0x610F70, 0x48C1A0 };
	WEAK symbol<unsigned int(const char** pName, int* type)> Scr_GetFunction{ 0x684F70, 0x610540, 0x48B790 };
	WEAK symbol<void()> Scr_ErrorInternal{ 0x690150, 0x61B720, 0x496890 };
	WEAK symbol<void()> Scr_LoadLevel{ 0x5AF770, 0x53AEB0, 0x3B2250 };
	WEAK symbol<void(unsigned int threadName, const char* codePos)> Scr_SetThreadPosition{ 0x685380, 0x610950, 0x48BBD0 };
	WEAK symbol<void(const char* fmt, ...)> Scr_Error{ 0x6900F0, 0x61B6C0, 0x496830 };
	WEAK symbol<void(int paramIndex, const char* fmt, ...)> Scr_Error2{ 0x691660, 0x61CC30, 0x497F10 };

	WEAK symbol<void()> GScr_LoadConsts{ 0x5CE110, 0x559860, 0x3E7E50 };
	WEAK symbol<unsigned int(unsigned int parentId, unsigned int name)> FindVariable{ 0x68A950, 0x615F20, 0x4910C0 };
	WEAK symbol<unsigned int(int entnum, unsigned int classnum)> FindEntityId{ 0x68A870, 0x615E40, 0x490FE0 };
	WEAK symbol<scr_string_t(unsigned int parentId, unsigned int id)> GetVariableName{ 0x68B270, 0x616840, 0x4919E0 };
	WEAK symbol<void(VariableValue* result, unsigned int classnum, int entnum, int offset)> GetEntityFieldValue{ 0x68F150, 0x61A720, 0x495870 };
	WEAK symbol<unsigned int(unsigned int)> GetObjectType{ 0x68B080, 0x616650, 0x4917F0 };
	WEAK symbol<unsigned int(unsigned int, unsigned int)> GetVariable{ 0x68B180, 0x616750, 0x4918F0 };
	WEAK symbol<void(unsigned int id)> RemoveRefToObject{ 0x68B3C0, 0x616990, 0x491B30 };
	WEAK symbol<void(uint64_t markPos)> LargeLocalResetToMark{ 0x287590, 0x236830, 0xEBD70 };
	WEAK symbol<void(const char* filename)> ProcessScript{ 0x68F1B0, 0x61A780, 0x4958D0 };

	WEAK symbol<unsigned int(unsigned int localId, const char* pos, unsigned int paramcount)> VM_Execute{ 0x6922A0, 0x61D870, 0x498B50 };
	
	WEAK symbol<const char*(scr_string_t stringValue)> SL_ConvertToString{ 0x688C90, 0x614260, 0x48F3D0 };
	WEAK symbol<unsigned int(char const* str)> SL_GetCanonicalString{ 0x685510, 0x610AE0, 0x48BD60 };

	WEAK symbol<void(errorParm_t code, const char* message, ...)> Com_Error{ 0x90750, 0x73AA0, 0x46F7C0 };

	WEAK symbol<void*(unsigned int size, unsigned int alignment, int selector)> PMem_AllocFromSource_NoDebug{ 0x769910, 0x6F56B0, 0x4D38D0 };
	WEAK symbol<uint8_t*(uint32_t*)> PMem_GetScriptMemory{ 0x4D6740, 0x461E80, 0x2A86F0 };

	WEAK symbol<void(int clearScripts)> G_ShutdownGame{ 0x562920, 0x4EE0F0, 0x3768C0 };
	
	WEAK symbol<void*> g_scr_func_table{ 0xAC9BE40, 0xBFA90C0, 0x9D1E000 };
	WEAK symbol<void*> g_scr_meth_table{ 0xAC9DC70, 0xBFAAEF0, 0x9D1FE30 };

	WEAK symbol<CmdArgs> sv_cmd_args{ 0xAA763C0, 0xBD83640, 0x97D7CB0 };
	WEAK symbol<CmdArgs> cmd_args{ 0xAA762D0, 0xBD83550, 0x97D7BC0 };
	WEAK symbol<cmd_function_s*> sv_cmd_functions{ 0xAA763B8, 0xBD83638, 0x97D7DA8 };
	WEAK symbol<cmd_function_s*> cmd_functions{ 0xAA762C8, 0xBD83548, 0x97D7CA8 };
	WEAK symbol<CmdText> cmd_textArray{ 0xAA764A8, 0xBD83728, 0x97D7D98 };
	WEAK symbol<const char*> command_whitelist{ 0xF91FC0, 0xFA8020, 0xAD7650 };
	WEAK symbol<unsigned int> cmd_funcCount{ 0xAA764C8, 0xBD83748, 0x97D7EB0 };
	WEAK symbol<void*> cmd_funcArray{ 0xAA764D0, 0xBD83750, 0x97D7DB0 };

	WEAK symbol<int> dvarCount{ 0x14DB5DC, 0x10FDBB4, 0xAEF3008 };
	WEAK symbol<dvar_t> dvarPool{ 0x2701C70, 0x26C30F0, 0xAEF3010 };

	WEAK symbol<int> keyCatchers{ 0x1BAF4E0, 0x1159EC0, 0x3425600 };
	WEAK symbol<PlayerKeyState> playerKeys{ 0x8B90F4C, 0x9E9C7BC, 0x3421FCC };

	WEAK symbol<int> sv_map_restart{ 0xBB4FE60, 0xCE5D0E0, 0xB39AD14 };
	WEAK symbol<int> sv_loadScripts{ 0xBB4FE64, 0xCE5D0E4, 0xB39AD18 };

	WEAK symbol<char> g_zones{ 0x58360E0, 0x57F7540, 0x6907D50 };
	
	WEAK symbol<int> sv_maxclients{ 0xC5FBA50, 0xD768150 };
	WEAK symbol<int> sv_migrate{ 0xBB4FE68, 0xCE5D0E8 };
	WEAK symbol<int> g_skipReadAlwaysLoadedAssets{ 0x11112E4, 0x10FD7D0 };
	WEAK symbol<int> frontend_state{ 0xE236DA0, 0xF92DCCC };
	WEAK symbol<char> databaseCompletedEvent2{ 0x27CEC67, 0x27900D3 };

	WEAK symbol<char> virtualLobby_Loaded{ 0x1BD36F8, 0x115ADAE };
	WEAK symbol<char> virtualLobby_Requested{ 0x1BD36F9, 0x115ADAF }; // SV_Startup's frontend allocation gate

	WEAK symbol<SOCKET> ip_socket{ 0xD8B0540, 0xEBE1DA0 };

	WEAK symbol<scrVarGlob_t> scr_VarGlob{ 0xB2FBF80, 0xC609200, 0xA37E180 };
	WEAK symbol<scrVmPub_t> scr_VmPub{ 0xBB0E1C0, 0xCE1B440, 0xAB903C0 };
	WEAK symbol<function_stack_t> scr_function_stack{ 0xBB18F60, 0xCE261E0, 0xAB9B160 };
	
	constexpr auto CMD_MAX_NESTING = 8;

	namespace sp
	{
		WEAK symbol<void(int savegame, int loadScripts)> SV_MapRestart{ 0, 0, 0x5883E0 };
		WEAK symbol<sp::playerState_s*(int clientNum)> SV_GetPlayerstateForClientNum{ 0, 0, 0x589830 };

		WEAK symbol<Weapon(const char* name)> G_GetWeaponForName{ 0, 0, 0x3D4A30 };
		WEAK symbol<bool(sp::playerState_s* ps, const Weapon* weapon, int dualWield, int startAlt,
			int a5, int a6)> G_GivePlayerWeapon{ 0, 0, 0x3D5010 };
		WEAK symbol<void(sp::playerState_s* ps, const Weapon* weapon, int hadWeapon)> G_InitializeAmmo{ 0, 0, 0x36EE60 };
		WEAK symbol<void(char clientNum, const Weapon* weapon)> G_SelectWeapon{ 0, 0, 0x3D59D0 };
		WEAK symbol<int(sp::playerState_s* ps, const Weapon* weapon)> G_TakePlayerWeapon{ 0, 0, 0x3D5DD0 };
		WEAK symbol<bool(sp::gentity_s* target, const sp::gentity_s* inflictor, sp::gentity_s* attacker,
			const float* direction, const float* point, int damage, int damageFlags, int meansOfDeath,
			const Weapon* weapon, bool isAlternate, int timeOffset, int hitLocation,
			unsigned int modelIndex, scr_string_t partName)> G_Damage{ 0, 0, 0x3669B0 };

		WEAK symbol<void(int localClientNum, const char* message, int type)> CG_GameMessage{ 0, 0, 0x20DDF0 };
		// SP CG_CalcViewValues (0x242430): refdef.view.org and the first view-axis row.
		WEAK symbol<const float> refdef_view_origin{ 0, 0, 0x2D59224 };
		WEAK symbol<const float> refdef_view_forward{ 0, 0, 0x2D59230 };
		WEAK symbol<sp::gentity_s> g_entities{ 0, 0, 0x8313160 };
	}

	namespace mp
	{
		WEAK symbol<void(mp::client_t* client, const char* reason, std::uint8_t notify)> SV_DropClient{ 0xF4030, 0xD2D70 };
		WEAK symbol<void(int migrate, int loadScripts)> SV_MapRestart{ 0x6D6F60, 0x662570 };
		WEAK symbol<mp::playerState_s*(int clientNum)> SV_GetPlayerstateForClientNum{ 0x6D9AF0, 0x665130 };

		WEAK symbol<Weapon(const char* name)> G_GetWeaponForName{ 0x5C4C00, 0x550340 };
		WEAK symbol<int(mp::playerState_s* ps, const Weapon* weapon, int dualWield, int startAlt,
			int a5, unsigned char sourceClientNum, unsigned short a7, unsigned short a8)> G_GivePlayerWeapon{ 0x5C5290, 0x5509D0 };
		WEAK symbol<void(mp::playerState_s* ps, const Weapon* weapon, int hadWeapon)> G_InitializeAmmo{ 0x55AD90, 0x4E6560 };
		WEAK symbol<void(char clientNum, const Weapon* weapon)> G_SelectWeapon{ 0x5C6120, 0x551860 };
		WEAK symbol<int(mp::playerState_s* ps, const Weapon* weapon)> G_TakePlayerWeapon{ 0x5C6490, 0x551BD0 };
		WEAK symbol<void(scr_entref_t entref)> PlayerCmd_Suicide{ 0x549F40, 0x4D8010 };

		WEAK symbol<mp::gentity_s*(const char* name, int customizationGroup)> SV_AddBot{ 0xF2650, 0xD13B0 };

		WEAK symbol<int(mp::gentity_s* entity)> SV_SpawnTestClient{ 0xF6AA0, 0xD57E0 };
		WEAK symbol<bool(char clientNum)> SV_HasAssignedTeam_Internal{ 0x6DF9D0, 0x66AFB0 };
		WEAK symbol<void(char clientNum, int team)> SV_SetAssignedTeam{ 0x6E1410, 0x66C9B0 };

		WEAK symbol<int> svs_time{ 0xC5FBA44, 0xD768144 };
		WEAK symbol<mp::client_t*> svs_clients{ 0xC5FBA58, 0xD768158 };
		WEAK symbol<mp::gentity_s> g_entities{ 0x9ED4430, 0xB1E3330 };
	}

	WEAK symbol<void(bool frontend, bool cg)> LUI_CoD_Init{ 0x318A00, 0x2C7DE0, 0x19CA30 };
	WEAK symbol<void()> LUI_CoD_Shutdown{ 0x31B6F0, 0x2CAAB0, 0x19EF50 };
	WEAK symbol<void(bool cg)> LUI_CoD_Restart{ 0x3200A0, 0x2CF460, 0x1A3810 };
	WEAK symbol<void()> LUI_EnterCriticalSection{ 0xBE8D0, 0x9E650, 0x18B1F0 };
	WEAK symbol<void()> LUI_LeaveCriticalSection{ 0xC5F80, 0xA5D10, 0x191FB0 };
	WEAK symbol<std::int64_t(void* transactionId)> AE_GenerateTransactionId{ 0x8390A0, 0x799A80 };
	WEAK symbol<bool(unsigned int controllerIndex, const void* transactionId)> AE_FetchUserAchievements{ 0x139350, 0x1185E0 };
	WEAK symbol<unsigned int(const char* reference)> BG_GetItemGUIDFromReference{ 0x6524C0, 0x5DDC10 };
	WEAK symbol<int(unsigned int controllerIndex, unsigned int itemGuid)> Inventory_GetItemQuantity{ 0x279480, 0x2282D0 };
	WEAK symbol<bool(unsigned int itemGuid)> Inventory_IsItemGuidAZMConsumable{ 0x652490, 0x5DDBE0 };

	namespace hks
	{
		WEAK symbol<lua_State*> lui_lua_state{ 0x1BD3D08, 0x1B69E70, 0x2A39810 };
		WEAK symbol<lua_State*> clientscript_lua_state{ 0x6CA66B8, 0x6C67C08, 0x1154638 };

		WEAK symbol<void(lua_State* s, const char* str, int l)> hksi_lua_pushlstring{ 0x104620, 0xE3410, 0x2B830 };
		WEAK symbol<HksObject*(HksObject* result, lua_State* s, const HksObject* table, const HksObject* key)> hks_obj_getfield{ 0x2D8580, 0x287960, 0x192211 };
		WEAK symbol<void(lua_State* s, const HksObject* tbl, const HksObject* key, const HksObject* val)> hks_obj_settable{ 0x2D96B0, 0x288A90, 0x14DA70 };
		WEAK symbol<HksObject*(HksObject* result, lua_State* s, const HksObject* table, const HksObject* key)> hks_obj_gettable{ 0x2D8AA0, 0x287E80, 0x14CE60 };
		WEAK symbol<void(lua_State* s, int nargs, int nresults, const unsigned int* pc)> vm_call_internal{ 0x308800, 0x2B7BE0, 0x17CDF0 };
		WEAK symbol<HashTable*(lua_State* s, unsigned int arraySize, unsigned int hashSize)> Hashtable_Create{ 0x2C8490, 0x277870, 0x13C850 };
		WEAK symbol<int(lua_State* s, int t)> hksi_luaL_ref{ 0x2DB960, 0x28AD40, 0x14FE50 };
		WEAK symbol<void(lua_State* s, int t, int ref)> hksi_luaL_unref{ 0x2DBC10, 0x28AFF0, 0x150100 };
		WEAK symbol<int(lua_State* s, const HksCompilerSettings* options, const char* buff, unsigned int sz, const char* name)> hksi_hksL_loadbuffer{ 0x2DA020, 0x289400, 0x14E510 };
		WEAK symbol<int(lua_State* state, void* compiler_options, void* reader, void* reader_data, const char* chunk_name)> load{ 0x2D7D10, 0x2870F0, 0x14C0D0 };
		WEAK symbol<int(lua_State* s, const char* what, lua_Debug* ar)> hksi_lua_getinfo{ 0x2DC230, 0x28B610, 0x150720 };
		WEAK symbol<int(lua_State* s, int level, lua_Debug* ar)> hksi_lua_getstack{ 0x2DCB60, 0x28BF40, 0x151050 };
		WEAK symbol<void(lua_State* s, const char* fmt, ...)> hksi_luaL_error{ 0x2DB890, 0x28AC70, 0x14FD80 };
		WEAK symbol<const char*> s_compilerTypeName{ 0xF7B340, 0xF913A0, 0xAC1830 };

		WEAK symbol<int(lua_State* s)> package_require{ 0x2C3080, 0x272460, 0x137440 };
		WEAK symbol<int(lua_State* s)> base_print{ 0x2BA3F0, 0x2697D0, 0x12E7B0 };

		// cclosure_Create is inlined, we'll use this to reimplement cclosure_Create
		WEAK symbol<void(lua_State* s, lua_function function, int num_upvalues, 
			const char* name, int internal_, int profilerTreatClosureAsFunc)> hksi_lua_pushcclosure{ 0x2DAB70, 0x289F50, 0x14F060 };
	}
}
