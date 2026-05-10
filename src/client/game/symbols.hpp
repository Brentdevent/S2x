#pragma once

#include "structs.hpp"
#include "loader/component_loader.hpp"

#define WEAK __declspec(selectany)

namespace game
{
	WEAK symbol<void (const char* text, int max_chars, const Font_s* font, int unk0, int unk1, int pixel_h, 
		float pos_x, float pos_y, float scale_x, float scale_y, float rotation, const float* color, long long style)> R_AddCmdDrawText{ 0x8BA480, 0x639430 };
	WEAK symbol<Font_s* (const char* name, int size)> R_RegisterFont{ 0x893D90, 0x612EB0 };

	WEAK symbol<Material* (const char* material)> Material_RegisterHandle{ 0x8AC5F0, 0x62B0E0 };

	WEAK symbol<void* (const char* text, int maxChars, Font_s* font, uint8_t unk0, uint8_t unk1, int fontHeight, float x, float y, float xScale, float yScale, 
		float rotation, const float* color, uint16_t styleFlags, float glowAlpha, int cursorPos, char cursor, const void* glowStyle)> AddBaseDrawTextCmd{ 0x8B80C0, 0x637510 };

	// 4th and 5th parameters are always 0, names are guessed.
	WEAK symbol<float (const char* text, int maxChars, Font_s* font, uint8_t iconFontIndex, uint8_t iconFontFlags)> R_TextWidth{ 0x894240, 0x613360 };
	WEAK symbol<int (Font_s* font)> R_GetFontHeight{ 0x763500, 0x4CC130 };
	WEAK symbol<void* (int style)> R_Font_GetLegacyFontStyle{ 0x893A90, 0x612BB0 };
	WEAK symbol<void (float x, float y, float width, float height, float s0, float t0, float s1, float t1,
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

	WEAK symbol<void (const int critical_section)> RtlEnterCriticalSection{ 0x7729B0, 0x4DA170 };
	WEAK symbol<void (const int critical_section)> RtlLeaveCriticalSection{ 0x772A20, 0x4DA1E0 };

	WEAK symbol<dvar_t* (const char* dvarName, bool value, DvarFlags flags)> Dvar_RegisterBool{ 0xB05E0, 0x4CE830 };
	WEAK symbol<dvar_t* (const char* dvarName, float value, float min, float max, DvarFlags flags)> Dvar_RegisterFloat{ 0xB0A50, 0x4CEA90 };
	WEAK symbol<dvar_t* (const char* dvarName, int value, int min, int max, DvarFlags flags)> Dvar_RegisterInt{ 0xB0C70, 0x4CEB00 };
	WEAK symbol<dvar_t* (const char* dvarName, const char* value, DvarFlags flags)> Dvar_RegisterString{ 0xB1460, 0x4CEDF0 };
	WEAK symbol<dvar_t* (const char* dvarName, const char* const* valueList, int defaultIndex, DvarFlags flags)> Dvar_RegisterEnum{ 0xB0970, 0x4CEA20 };
	WEAK symbol<dvar_t* (const char* dvarName, float x, float y, float min, float max, DvarFlags flags)> Dvar_RegisterVec2{ 0xB1520, 0x4CF000 };
	WEAK symbol<dvar_t* (const char* dvarName, float x, float y, float z, float min, float max, DvarFlags flags)> Dvar_RegisterVec3{ 0xB1620, 0x4CF100 };
	WEAK symbol<dvar_t* (const char* dvarName, float x, float y, float z, float w, float min, float max, DvarFlags flags)> Dvar_RegisterVec4{ 0xB1860, 0x4CF2A0 };
	WEAK symbol<dvar_t* (const char* dvarName, float x, float y, float z, float max, DvarFlags flags)> Dvar_RegisterVec3Color{ 0xB1760, 0x4CF220 };

	WEAK symbol<dvar_t* (const char* dvarName, bool value, DvarFlags flags)> Dvar_RegisterBoolProtected{ 0xB06A0 };
	WEAK symbol<dvar_t* (const char* dvarName, float value, float min, float max, DvarFlags flags)> Dvar_RegisterFloatProtected{ 0xB0B60 };
	WEAK symbol<dvar_t* (const char* dvarName, int value, int min, int max, DvarFlags flags)> Dvar_RegisterIntProtected{ 0xB0D40 };

	WEAK symbol<dvar_t* (const char* dvarName)> Dvar_FindMalleableVar{ 0xAF8E0, 0x4CDBE0 };
	WEAK symbol<void (dvar_t* dvar, DvarFlags flags)> Dvar_SetFlags{ 0xAF370 };

	WEAK symbol<bool (const char* dvarName)> Dvar_GetBool{ 0xAFA70 };
	WEAK symbol<float (const char* dvarName)> Dvar_GetFloat{ 0xAFBC0 };
	WEAK symbol<int (const char* dvarName)> Dvar_GetInt{ 0xAFCB0 };
	WEAK symbol<const char* (const char* dvarName)> Dvar_GetString{ 0xAFD20 };
	WEAK symbol<int64_t (const char* dvarName)> Dvar_GetInt64{ 0xAFC10 };

	WEAK symbol<const char* (dvar_t* dvar, bool decode, DvarValue* dValue)> Dvar_ValueToString{ 0xB4520, 0x4D1230 };

	WEAK symbol<dvar_t* (const char* dvarName, bool value)> Dvar_SetBoolByName{ 0xB2020 };
	WEAK symbol<dvar_t* (const char* dvarName, int value)> Dvar_SetIntByName{ 0xB2A40 };
	WEAK symbol<dvar_t* (const char* dvarName, float value)> Dvar_SetFloatByName{ 0xB2790 };
	WEAK symbol<dvar_t* (const char* dvarName, const char* value)> Dvar_SetStringByName{ 0xB3070 };

	WEAK symbol<void (const char* dvarName, const char* string)> Dvar_SetCommand{ 0xB2360 };
	WEAK symbol<void (dvar_t* dvar, bool value)> Dvar_SetBool{ 0xB1FD0 };
	WEAK symbol<void (dvar_t* dvar, float value)> Dvar_SetFloat{ 0xB2700 };
	WEAK symbol<void (dvar_t* dvar, int value)> Dvar_SetInt{ 0xB29C0 };
	WEAK symbol<void (dvar_t* dvar, const char* value)> Dvar_SetString{ 0xB2E30 };

	WEAK symbol<const ScreenPlacement* (const LocalClientNum_t localClientNum)> ScrPlace_GetViewPlacement{ 0x4A01B0, 0x272660 };

	WEAK symbol<CmdArgs> sv_cmd_args{ 0xAA763C0 };
	WEAK symbol<cmd_function_s*> sv_cmd_functions{ 0xAA763B8, 0x97D7DA8 };
	WEAK symbol<CmdArgs> cmd_args{ 0xAA762D0, 0x97D7CB0 };
	WEAK symbol<cmd_function_s*> cmd_functions{ 0xAA762C8, 0x97D7CA8 };
	WEAK symbol<CmdText> cmd_textArray{ 0xAA764A8, 0x97D7D98 };
	WEAK symbol<const char*> command_whitelist{ 0xF91FC0, 0xAD7650 };
	WEAK symbol<unsigned int> cmd_funcCount{ 0xAA764C8, 0x97D7EB0 };
	WEAK symbol<void*> cmd_funcArray{ 0xAA764D0, 0x97D7DB0 };

	WEAK symbol<int> dvarCount{ 0x14DB5DC, 0xAEF3008 };
	WEAK symbol<dvar_t> dvarPool{ 0x2701C70, 0xAEF3010 };

	WEAK symbol<int> keyCatchers{ 0x1BAF4E0, 0x3425600 };
	WEAK symbol<PlayerKeyState> playerKeys{ 0x8B90F4C, 0x3421FCC };
}
