#pragma once

#include "structs.hpp"
#include "loader/component_loader.hpp"

#define WEAK __declspec(selectany)

namespace game
{
	WEAK symbol<void(const char* text, int max_chars, const Font_s* font, int unk0, int unk1, int pixel_h, 
		float pos_x, float pos_y, float scale_x, float scale_y, float rotation, const float* color, long long style)> R_AddCmdDrawText{ 0x8BA480, 0x639430 };
	WEAK symbol<Font_s*(const char* name, int size)> R_RegisterFont{ 0x893D90, 0x612EB0 };

	WEAK symbol<Material*(const char* material)> Material_RegisterHandle{ 0x8AC5F0, 0x62B0E0 };

	WEAK symbol<void*(const char* text, int maxChars, Font_s* font, uint8_t unk0, uint8_t unk1, int fontHeight, float x, float y, float xScale, float yScale, 
		float rotation, const float* color, uint16_t styleFlags, float glowAlpha, int cursorPos, char cursor, const void* glowStyle)> AddBaseDrawTextCmd{ 0x8B80C0, 0x637510 };

	// 4th and 5th parameters are always 0, names are guessed.
	WEAK symbol<float(const char* text, int maxChars, Font_s* font, uint8_t iconFontIndex, uint8_t iconFontFlags)> R_TextWidth{ 0x894240, 0x613360 };
	WEAK symbol<int(Font_s* font)> R_GetFontHeight{ 0x763500, 0x4CC130 };
	WEAK symbol<void*(int style)> R_Font_GetLegacyFontStyle{ 0x893A90, 0x612BB0 };
	WEAK symbol<void(float x, float y, float width, float height, float s0, float t0, float s1, float t1,
		const float* color, Material* material)> R_AddCmdDrawStretchPic{ 0x8B9A00, 0x638AB0 };

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
	
	WEAK symbol<void()> Com_Quit_f{ 0x9A130, 0x471D30 };
	WEAK symbol<void()> j_Com_Quit_f{ 0x78A5E0, 0x4E9AF0 };
	WEAK symbol<void()> SV_FastRestart_f{ 0x6D6BA0, 0x5885D0 };
	WEAK symbol<void()> Cbuf_AddServerText_f{ 0x64A1A0, 0x464E60 };

	WEAK symbol<void(const int critical_section)> RtlEnterCriticalSection{ 0x7729B0, 0x4DA170 };
	WEAK symbol<void(const int critical_section)> RtlLeaveCriticalSection{ 0x772A20, 0x4DA1E0 };

	WEAK symbol<dvar_t*(const char* dvarName, bool value, DvarFlags flags)> Dvar_RegisterBool{ 0xB05E0, 0x4CE830 };
	WEAK symbol<dvar_t*(const char* dvarName, float value, float min, float max, DvarFlags flags)> Dvar_RegisterFloat{ 0xB0A50, 0x4CEA90 };
	WEAK symbol<dvar_t*(const char* dvarName, int value, int min, int max, DvarFlags flags)> Dvar_RegisterInt{ 0xB0C70, 0x4CEB00 };
	WEAK symbol<dvar_t*(const char* dvarName, const char* value, DvarFlags flags)> Dvar_RegisterString{ 0xB1460, 0x4CEDF0 };
	WEAK symbol<dvar_t*(const char* dvarName, const char* const* valueList, int defaultIndex, DvarFlags flags)> Dvar_RegisterEnum{ 0xB0970, 0x4CEA20 };
	WEAK symbol<dvar_t*(const char* dvarName, float x, float y, float min, float max, DvarFlags flags)> Dvar_RegisterVec2{ 0xB1520, 0x4CF000 };
	WEAK symbol<dvar_t*(const char* dvarName, float x, float y, float z, float min, float max, DvarFlags flags)> Dvar_RegisterVec3{ 0xB1620, 0x4CF100 };
	WEAK symbol<dvar_t*(const char* dvarName, float x, float y, float z, float w, float min, float max, DvarFlags flags)> Dvar_RegisterVec4{ 0xB1860, 0x4CF2A0 };
	WEAK symbol<dvar_t*(const char* dvarName, float x, float y, float z, float max, DvarFlags flags)> Dvar_RegisterVec3Color{ 0xB1760, 0x4CF220 };

	WEAK symbol<dvar_t*(const char* dvarName, bool value, DvarFlags flags)> Dvar_RegisterBoolProtected{ 0xB06A0 };
	WEAK symbol<dvar_t*(const char* dvarName, float value, float min, float max, DvarFlags flags)> Dvar_RegisterFloatProtected{ 0xB0B60 };
	WEAK symbol<dvar_t*(const char* dvarName, int value, int min, int max, DvarFlags flags)> Dvar_RegisterIntProtected{ 0xB0D40 };

	WEAK symbol<dvar_t*(const char* dvarName)> Dvar_FindMalleableVar{ 0xAF8E0, 0x4CDBE0 };
	WEAK symbol<void (dvar_t* dvar, DvarFlags flags)> Dvar_SetFlags{ 0xAF370 };

	WEAK symbol<bool(const char* dvarName)> Dvar_GetBool{ 0xAFA70 };
	WEAK symbol<float(const char* dvarName)> Dvar_GetFloat{ 0xAFBC0 };
	WEAK symbol<int(const char* dvarName)> Dvar_GetInt{ 0xAFCB0 };
	WEAK symbol<const char*(const char* dvarName)> Dvar_GetString{ 0xAFD20 };
	WEAK symbol<int64_t(const char* dvarName)> Dvar_GetInt64{ 0xAFC10 };

	WEAK symbol<const char*(dvar_t* dvar, bool decode, DvarValue* dValue)> Dvar_ValueToString{ 0xB4520, 0x4D1230 };

	WEAK symbol<dvar_t*(const char* dvarName, bool value)> Dvar_SetBoolByName{ 0xB2020 };
	WEAK symbol<dvar_t*(const char* dvarName, int value)> Dvar_SetIntByName{ 0xB2A40 };
	WEAK symbol<dvar_t*(const char* dvarName, float value)> Dvar_SetFloatByName{ 0xB2790 };
	WEAK symbol<dvar_t*(const char* dvarName, const char* value)> Dvar_SetStringByName{ 0xB3070 };

	WEAK symbol<void(const char* dvarName, const char* string)> Dvar_SetCommand{ 0xB2360 };
	WEAK symbol<void(dvar_t* dvar, bool value)> Dvar_SetBool{ 0xB1FD0 };
	WEAK symbol<void(dvar_t* dvar, float value)> Dvar_SetFloat{ 0xB2700 };
	WEAK symbol<void(dvar_t* dvar, int value)> Dvar_SetInt{ 0xB29C0 };
	WEAK symbol<void(dvar_t* dvar, const char* value)> Dvar_SetString{ 0xB2E30 };

	WEAK symbol<const ScreenPlacement*(const LocalClientNum_t localClientNum)> ScrPlace_GetViewPlacement{ 0x4A01B0, 0x272660 };

	WEAK symbol<const char*(int index)> Cmd_Argv{ 0x8D9F0 };
	WEAK symbol<void(const char* text_in)> SV_Cmd_TokenizeString{ 0x64C070, 0x4665A0 };
	WEAK symbol<void(const char* text_in)> Cmd_TokenizeString{ 0x64B970, 0x465EA0 };
	WEAK symbol<void()> SV_Cmd_EndTokenizedString{ 0x64C030, 0x466560 };
	WEAK symbol<void()> Cmd_EndTokenizedString{ 0x64AA00, 0x465410 };
	WEAK symbol<void(const char* cmdName, void(__fastcall* function)(), cmd_function_s* allocedCmd)> Cmd_AddCommandInternal{ 0x64A6B0, 0x465150 };
	WEAK symbol<void(const char* cmdName, void(__fastcall* function)(), cmd_function_s* allocedCmd)> Cmd_AddServerCommandInternal{ 0x64A720, 0x4651C0 };

	WEAK symbol<void(int localClientNum, const char* map, bool mapIsPreloaded)> SV_StartMap{ 0x6D8200 };
	WEAK symbol<void(const char* reason)> SV_Shutdown{ 0x6DBF50 };
	WEAK symbol<bool()> SV_Loaded{ 0x6DB810 };
	WEAK symbol<void(netadr_s* from, bool allowBotKick)> SV_DirectConnect{ 0xF31D0 };

	WEAK symbol<void(XZoneInfo* zoneInfo, unsigned int zoneCount, DBSyncMode syncMode)> DB_LoadXAssets{ 0xA4F60, 0x2ADB50 };
	WEAK symbol<void(const char* zoneName, int zoneFlags, int isBaseMap)> DB_TryLoadXFileInternal{ 0xACE30, 0x2AF980 };
	WEAK symbol<void(uint16_t* zoneIndices, unsigned int zoneCount, bool createDefault)> DB_UnloadXZones{ 0xADE50, 0x2B05D0 };
	WEAK symbol<void()> DB_SyncXAsset{ 0xACA60 };

	WEAK symbol<void(const char* mapName, const char* gameType)> CL_PreloadMap{ 0x83950 };
	WEAK symbol<void(const char* mapName)> CL_PreloadMap2{ 0x837E0 };
	WEAK symbol<void(int a, int b)> CL_VirtualLobbyShutdown{ 0x8BB80 };
	WEAK symbol<bool(int localClientNum, netadr_s* from, msg_t* msg, int time)> CL_DispatchConnectionlessPacket{ 0x6F7E0 };
	WEAK symbol<void(int localClientNum, void* sessionInfo, netadr_s* to, const char* mapname, const char* gametype)> CL_ConnectAndPreloadMap{ 0x6CCA0 };
	WEAK symbol<void()> CL_Connect{ 0x6D0A0 };

	WEAK symbol<void(int localClientNum, const char** args)> UI_RunMenuScript{ 0x746A50 };
	WEAK symbol<int(const char* mapName)> UI_GetListIndexFromMapName{ 0x650510 };
	WEAK symbol<void(const char* mapname, const char* gametype)> UI_SetMap{ 0x74A050 };
	WEAK symbol<void(int localClientNum)> UI_Map{ 0x744C30 };

	WEAK symbol<std::uint16_t(char* dst, const char* src, int length)> Sys_ChecksumCopy{ 0x75FB80 };
	WEAK symbol<int(netsrc_t sock, int length, const void* data, const netadr_s* to)> Sys_SendPacket{ 0x7B0F50 };
	WEAK symbol<int(netsrc_t sock, int length, const void* data, const netadr_s* to)> NET_SendPacket{ 0x66E290 };
	WEAK symbol<int(const netadr_s* a, const netadr_s* b)> NET_CompareAdr{ 0x66DAE0 };
	WEAK symbol<int(const netadr_s* a, const netadr_s* b)> NET_CompareBaseAdr{ 0x66DB50 };
	WEAK symbol<int(const char* address, netadr_s* out)> NET_StringToAdr{ 0x66E3E0 };
	WEAK symbol<void(const netadr_s* address, sockaddr* s)> NetadrToSockadr{ 0x75F9E0 };

	WEAK symbol<void*(int unk)> Lobby_GetPartyData{ 0x47D050 };
	WEAK symbol<void(void* partyData, const char* gametype)> Party_SetGameType{ 0x1973A0 };
	WEAK symbol<void(void* partyData, const char* mapname)> Party_SetMapName{ 0x1973C0 };

	WEAK symbol<bool()> BG_BotFastFileEnabled{ 0x3879A0 };
	WEAK symbol<bool()> BG_BotSystemEnabled{ 0x388180 };
	WEAK symbol<bool(void* unk)> BG_AgentSystemEnabled{ 0x387BC0 };
	WEAK symbol<bool()> BG_BotsUsingTeamDifficulty{ 0x388310 };

	WEAK symbol<std::uint64_t()> BG_NetDataChecksum{ 0x38BD70 };

	WEAK symbol<CmdArgs> sv_cmd_args{ 0xAA763C0, 0x97D7CB0 };
	WEAK symbol<CmdArgs> cmd_args{ 0xAA762D0, 0x97D7BC0 };
	WEAK symbol<cmd_function_s*> sv_cmd_functions{ 0xAA763B8, 0x97D7DA8 };
	WEAK symbol<cmd_function_s*> cmd_functions{ 0xAA762C8, 0x97D7CA8 };
	WEAK symbol<CmdText> cmd_textArray{ 0xAA764A8, 0x97D7D98 };
	WEAK symbol<const char*> command_whitelist{ 0xF91FC0, 0xAD7650 };
	WEAK symbol<unsigned int> cmd_funcCount{ 0xAA764C8, 0x97D7EB0 };
	WEAK symbol<void*> cmd_funcArray{ 0xAA764D0, 0x97D7DB0 };

	WEAK symbol<int> dvarCount{ 0x14DB5DC, 0xAEF3008 };
	WEAK symbol<dvar_t> dvarPool{ 0x2701C70, 0xAEF3010 };

	WEAK symbol<int> keyCatchers{ 0x1BAF4E0, 0x3425600 };
	WEAK symbol<PlayerKeyState> playerKeys{ 0x8B90F4C, 0x3421FCC };

	WEAK symbol<int> sv_map_restart{ 0xBB4FE60, 0xB39AD14 };
	WEAK symbol<int> sv_loadScripts{ 0xBB4FE64, 0xB39AD18 };

	WEAK symbol<char> g_zones{ 0x58360E0, 0x6907D50 };
	
	WEAK symbol<int> sv_maxclients{ 0xC5FBA50 };
	WEAK symbol<int> sv_migrate{ 0xBB4FE68 };

	WEAK symbol<char> virtualLobby_Loaded{ 0x1BD36F8 };

	WEAK symbol<SOCKET> ip_socket{ 0xD8B0540 };
	
	constexpr auto CMD_MAX_NESTING = 8;

	namespace sp
	{
		WEAK symbol<void(int savegame, int loadScripts)> SV_MapRestart{ 0x0, 0x5883E0 };
	}

	namespace mp
	{
		WEAK symbol<void(int migrate, int loadScripts)> SV_MapRestart{ 0x6D6F60 };
		WEAK symbol<mp::gentity_s*(const char* name, int customizationGroup)> SV_AddBot{ 0xF2650 };

		WEAK symbol<int(mp::gentity_s* entity)> SV_SpawnTestClient{ 0xF6AA0 };

		WEAK symbol<mp::client_t*> svs_clients{ 0xC5FBA58 };
		WEAK symbol<mp::gentity_s> g_entities{ 0x9ED4430 };
	}
}
