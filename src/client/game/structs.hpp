#pragma once

#ifdef __cplusplus
namespace game
{
#endif
	typedef float vec_t;
	typedef vec_t vec2_t[2];
	typedef vec_t vec3_t[3];
	typedef vec_t vec4_t[4];

	struct Material
	{
		const char* name;
	};

	struct Glyph
	{
		uint16_t letter;
		char x0;
		char y0;
		unsigned char dx;
		unsigned char pixelWidth;
		unsigned char pixelHeight;
		float s0;
		float t0;
		float s1;
		float t1;
	};

	// Taken from S1, probably not correct for all cases but good enough for now
	struct Font_s
	{
		const char* fontName;
		int pixelHeight; 
		int glyphCount;
		Material* material;
		Material* glowMaterial;
		Glyph* glyphs;
	};

	struct CmdText
	{
		char* data;
		int maxsize;
		int cmdsize;
	};

	// Not really correct in all cases but good enough for now
	enum keyNum_t
	{
		K_NONE = 0x0,
		K_FIRSTGAMEPADBUTTON_RANGE_1 = 0x1,
		K_BUTTON_A = 0x1,
		K_BUTTON_B = 0x2,
		K_BUTTON_X = 0x3,
		K_BUTTON_Y = 0x4,
		K_BUTTON_LSHLDR = 0x5,
		K_BUTTON_RSHLDR = 0x6,
		K_LASTGAMEPADBUTTON_RANGE_1 = 0x6,
		K_BS = 0x8,
		K_TAB = 0x9,
		K_ENTER = 0xD,
		K_FIRSTGAMEPADBUTTON_RANGE_2 = 0xE,
		K_BUTTON_START = 0xE,
		K_BUTTON_BACK = 0xF,
		K_BUTTON_LSTICK = 0x10,
		K_BUTTON_RSTICK = 0x11,
		K_BUTTON_LTRIG = 0x12,
		K_BUTTON_RTRIG = 0x13,
		K_DPAD_UP = 0x14,
		K_FIRSTDPAD = 0x14,
		K_DPAD_DOWN = 0x15,
		K_DPAD_LEFT = 0x16,
		K_DPAD_RIGHT = 0x17,
		K_BUTTON_LSTICK_ALTIMAGE2 = 0x10,
		K_BUTTON_RSTICK_ALTIMAGE2 = 0x11,
		K_BUTTON_LSTICK_ALTIMAGE = 0xBC,
		K_BUTTON_RSTICK_ALTIMAGE = 0xBD,
		K_LASTDPAD = 0x17,
		K_LASTGAMEPADBUTTON_RANGE_2 = 0x17,
		K_ESCAPE = 0x1B,
		K_FIRSTGAMEPADBUTTON_RANGE_3 = 0x1C,
		K_APAD_UP = 0x1C,
		K_FIRSTAPAD = 0x1C,
		K_APAD_DOWN = 0x1D,
		K_APAD_LEFT = 0x1E,
		K_APAD_RIGHT = 0x1F,
		K_LASTAPAD = 0x1F,
		K_LASTGAMEPADBUTTON_RANGE_3 = 0x1F,
		K_SPACE = 0x20,
		K_GRAVE = 0x60,
		K_TILDE = 0x7E,
		K_BACKSPACE = 0x7F,
		K_ASCII_FIRST = 0x80,
		K_ASCII_181 = 0x80,
		K_ASCII_191 = 0x81,
		K_ASCII_223 = 0x82,
		K_ASCII_224 = 0x83,
		K_ASCII_225 = 0x84,
		K_ASCII_228 = 0x85,
		K_ASCII_229 = 0x86,
		K_ASCII_230 = 0x87,
		K_ASCII_231 = 0x88,
		K_ASCII_232 = 0x89,
		K_ASCII_233 = 0x8A,
		K_ASCII_236 = 0x8B,
		K_ASCII_241 = 0x8C,
		K_ASCII_242 = 0x8D,
		K_ASCII_243 = 0x8E,
		K_ASCII_246 = 0x8F,
		K_ASCII_248 = 0x90,
		K_ASCII_249 = 0x91,
		K_ASCII_250 = 0x92,
		K_ASCII_252 = 0x93,
		K_END_ASCII_CHARS = 0x94,
		K_COMMAND = 0x96,
		K_CAPSLOCK = 0x97,
		K_POWER = 0x98,
		K_PAUSE = 0x99,
		K_UPARROW = 0x9A,
		K_DOWNARROW = 0x9B,
		K_LEFTARROW = 0x9C,
		K_RIGHTARROW = 0x9D,
		K_ALT = 0x9E,
		K_CTRL = 0x9F,
		K_SHIFT = 0xA0,
		K_INS = 0xA1,
		K_DEL = 0xA2,
		K_PGDN = 0xA3,
		K_PGUP = 0xA4,
		K_HOME = 0xA5,
		K_END = 0xA6,
		K_F1 = 0xA7,
		K_F2 = 0xA8,
		K_F3 = 0xA9,
		K_F4 = 0xAA,
		K_F5 = 0xAB,
		K_F6 = 0xAC,
		K_F7 = 0xAD,
		K_F8 = 0xAE,
		K_F9 = 0xAF,
		K_F10 = 0xB0,
		K_F11 = 0xB1,
		K_F12 = 0xB2,
		K_F13 = 0xB3,
		K_F14 = 0xB4,
		K_F15 = 0xB5,
		K_KP_HOME = 0xB6,
		K_KP_UPARROW = 0xB7,
		K_KP_PGUP = 0xB8,
		K_KP_LEFTARROW = 0xB9,
		K_KP_5 = 0xBA,
		K_KP_RIGHTARROW = 0xBB,
		K_KP_END = 0xBC,
		K_KP_DOWNARROW = 0xBD,
		K_KP_PGDN = 0xBE,
		K_KP_ENTER = 0xBF,
		K_KP_INS = 0xC0,
		K_KP_DEL = 0xC1,
		K_KP_SLASH = 0xC2,
		K_KP_MINUS = 0xC3,
		K_KP_PLUS = 0xC4,
		K_KP_NUMLOCK = 0xC5,
		K_KP_STAR = 0xC6,
		K_KP_EQUALS = 0xC7,
		K_MOUSE1 = 0xC8,
		K_MOUSE2 = 0xC9,
		K_MOUSE3 = 0xCA,
		K_MOUSE4 = 0xCB,
		K_MOUSE5 = 0xCC,
		K_MWHEELDOWN = 0xCE,
		K_MWHEELUP = 0xCF,
		//K_AUX1 = 0xCF,
		K_AUX2 = 0xD0,
		K_AUX3 = 0xD1,
		K_AUX4 = 0xD2,
		K_AUX5 = 0xD3,
		K_AUX6 = 0xD4,
		K_AUX7 = 0xD5,
		K_AUX8 = 0xD6,
		K_AUX9 = 0xD7,
		K_AUX10 = 0xD8,
		K_AUX11 = 0xD9,
		K_AUX12 = 0xDA,
		K_AUX13 = 0xDB,
		K_AUX14 = 0xDC,
		K_AUX15 = 0xDD,
		K_AUX16 = 0xDE,
		K_LAST_KEY = 0xDF
	};

	struct KeyState
	{
		int unk;
		int down;
		int repeats;
		int binding;
	};

	struct PlayerKeyState
	{
		int overstrikeMode;
		int anyKeyDown;
		char __pad0[0x40];
		KeyState keys[256];
		char __pad1[0x364];
	}; static_assert(sizeof(PlayerKeyState) == 0x13AC);

	enum DvarSetSource : std::uint32_t
	{
		DVAR_SOURCE_INTERNAL = 0x0,
		DVAR_SOURCE_EXTERNAL = 0x1,
		DVAR_SOURCE_SCRIPT = 0x2,
		DVAR_SOURCE_UISCRIPT = 0x3,
		DVAR_SOURCE_SERVERCMD = 0x4,
		DVAR_SOURCE_NUM = 0x5,
	};

	enum DvarFlags : std::uint32_t
	{
		DVAR_FLAG_NONE = 0,					// DVAR_NOFLAG
		DVAR_FLAG_SAVED = 0x1,				// DVAR_ARCHIVE
		DVAR_FLAG_LATCHED = 0x2,			// DVAR_LATCH
		DVAR_FLAG_CHEAT = 0x4,				// DVAR_CHEAT
		DVAR_FLAG_REPLICATED = 0x8,			// DVAR_CODINFO
		DVAR_FLAG_NETWORK = 0x10,			// DVAR_SCRIPTINFO
		DVAR_FLAG_TEMP = 0x20,				// DVAR_TEMP
		DVAR_FLAG_TRUE_SAVED = 0x40,		// DVAR_SAVED
		DVAR_FLAG_INTERNAL = 0x80,			// DVAR_INTERNAL
		DVAR_FLAG_EXTERNAL = 0x100,			// DVAR_EXTERNAL
		DVAR_FLAG_USERINFO = 0x200,			// DVAR_USERINFO
		DVAR_FLAG_SERVERINFO = 0x400,		// DVAR_SERVERINFO
		DVAR_FLAG_WRITE = 0x800,			// DVAR_ROM
		DVAR_FLAG_SYSTEMINFO = 0x1000,		// DVAR_SYSTEMINFO
		DVAR_FLAG_READ = 0x2000,			// DVAR_INIT
		DVAR_FLAG_CHANGEABLE_RESET = 0x4000,
		DVAR_FLAG_AUTOEXEC = 0x8000,

		DVAR_UNADDABLE_FLAGS = DVAR_FLAG_LATCHED | DVAR_FLAG_CHEAT | DVAR_FLAG_EXTERNAL | DVAR_FLAG_WRITE | DVAR_FLAG_READ
	};

	enum DvarType : std::uint8_t
	{
		DVAR_TYPE_BOOL = 0x0,
		DVAR_TYPE_FLOAT = 0x1,
		DVAR_TYPE_FLOAT_2 = 0x2,
		DVAR_TYPE_FLOAT_3 = 0x3,
		DVAR_TYPE_FLOAT_4 = 0x4,
		DVAR_TYPE_INT = 0x5,
		DVAR_TYPE_ENUM = 0x6,
		DVAR_TYPE_STRING = 0x7,
		DVAR_TYPE_COLOR = 0x8,
		DVAR_TYPE_FLOAT_3_COLOR = 0x9,
		DVAR_TYPE_BOOL_PROTECTED = 0xA,
		DVAR_TYPE_FLOAT_PROTECTED = 0xB,
		DVAR_TYPE_INT_PROTECTED = 0xC,
		DVAR_TYPE_COUNT = 0xD,
	};

	union DvarValue
	{
		bool enabled;
		int integer;
		unsigned int unsignedInt;
		float value;
		float vector[4];
		const char* string;
		std::uint8_t color[4];
	}; static_assert(sizeof(DvarValue) == 0x10);

	union DvarLimits
	{
		struct
		{
			int stringCount;
			const char* const* strings;
		} enumeration;

		struct
		{
			int min;
			int max;
		} integer;

		struct
		{
			float min;
			float max;
		} value;

		struct
		{
			float min;
			float max;
		} vector;
	}; static_assert(sizeof(DvarLimits) == 0x10);

	struct dvar_t
	{
		const char* name;
		uint32_t flags;
		DvarType type;
		bool modified;
		char __pad[2];

		DvarValue current;
		DvarValue latched;
		DvarValue reset;

		DvarLimits domain;

		bool(__fastcall* domainFunc)(dvar_t*, DvarValue);
		dvar_t* hashNext;
	}; static_assert(sizeof(dvar_t) == 0x60);

	struct ScreenPlacement
	{
		vec2_t scaleVirtualToReal;
		vec2_t scaleVirtualToFull;
		vec2_t scaleRealToVirtual;
		vec2_t realViewportPosition;
		vec2_t realViewportSize;
		vec2_t virtualViewableMin;
		vec2_t virtualViewableMax;
		vec2_t realViewableMin;
		vec2_t realViewableMax;
		vec2_t virtualAdjustableMin;
		vec2_t virtualAdjustableMax;
		vec2_t realAdjustableMin;
		vec2_t realAdjustableMax;
		float subScreenLeft;
	};

	enum LocalClientNum_t : std::int32_t
	{
		LOCAL_CLIENT_INVALID = -1,
		LOCAL_CLIENT_0 = 0x0,
		LOCAL_CLIENT_1 = 0x1,
		LOCAL_CLIENT_2 = 0x2,
		LOCAL_CLIENT_3 = 0x3,
		LOCAL_CLIENT_LAST = 0x3,
		LOCAL_CLIENT_COUNT = 0x4,
	};

	struct CmdArgs
	{
		int nesting;
		int localClientNum[8];
		int controllerIndex[8];
		int argc[8];
		const char** argv[8];
	};

	struct cmd_function_s
	{
		cmd_function_s* next;
		const char* name;
		void(__cdecl* function)();
	};

	enum DBSyncMode
	{
		DB_LOAD_ASYNC = 0x0,
		DB_LOAD_SYNC = 0x1,
		DB_LOAD_ASYNC_WAIT_ALLOC = 0x2,
		DB_LOAD_ASYNC_FORCE_FREE = 0x3,
		DB_LOAD_ASYNC_NO_SYNC_THREADS = 0x4,
	};

	struct XZoneInfo
	{
		const char* name;
		int allocFlags;
		int freeFlags;
	};

	struct StringTableCell
	{
		const char* string;
		int hash;
	};

	struct StringTable
	{
		const char* name;
		int columnCount;
		int rowCount;
		StringTableCell* values;
	};

	enum netadrtype_t : int
	{
		NA_BOT = 0x0,
		NA_BAD = 0x1,
		NA_LOOPBACK = 0x2,
		NA_BROADCAST = 0x3,
		NA_IP = 0x4,
	};

	enum netsrc_t : int
	{
		NS_CLIENT1 = 0x0,
		NS_MAXCLIENTS = 0x1,
		NS_SERVER = 0x2,
		NS_PACKET = 0x3,
		NS_INVALID_NETSRC = 0x4,
	};

	struct netadr_s
	{
		netadrtype_t type;
		union
		{
			uint8_t ip[4];
			uint32_t addr;
		};
		uint16_t port;
		netsrc_t localNetID;
		uint32_t addrHandleIndex;
	};

	struct msg_t
	{
		int overflowed;
		int readOnly;
		char* data;
		char* splitData;
		int maxsize;
		int cursize;
		int splitSize;
		int readcount;
		int bit;
		int lastEntityRef;
		netsrc_t targetLocalNetID;
		int useZlib;
	};
	static_assert(sizeof(msg_t) == 0x38);

	namespace sp
	{
		struct playerState_s
		{
			// ... incomplete
		};

		struct gclient_s
		{
			char __pad0[0xF24C];
			int autoMantle;
			int sprintCancel;

			// ... incomplete
		};

		struct gentity_s
		{
			char __pad0[0x1C0];
			gclient_s* client;
			char __pad1C8[0x400 - 0x1C8];
		};
		static_assert(sizeof(gentity_s) == 0x400);
		static_assert(offsetof(gentity_s, client) == 0x1C0);
		static_assert(offsetof(gclient_s, autoMantle) == 0xF24C);
		static_assert(offsetof(gclient_s, sprintCancel) == 0xF250);

		struct XZone
		{
			char __pad0[0x328];
			char name[0x40];
			int flags;
			char __pad1[0x12C];
		};
		static_assert(offsetof(XZone, name) == 0x328);
		static_assert(offsetof(XZone, flags) == 0x368);
		static_assert(sizeof(XZone) == 0x498);
	}

	namespace mp
	{
		struct playerState_s
		{
			char clientNum;
			bool cursorHintDualWield;
			uint8_t pm_type;

			// ... incomplete
		};

		struct gclient_s
		{
			playerState_s ps;
			char __pad0[0x5AB8 - sizeof(playerState_s)];
			int team;

			// ... incomplete
		};
		static_assert(offsetof(gclient_s, team) == 0x5AB8);

		struct gentity_s
		{
			char __pad0[0x258];
			gclient_s* client;
			char __pad260[0x418 - 0x260];
		};
		static_assert(sizeof(gentity_s) == 0x418);
		static_assert(offsetof(gentity_s, client) == 0x258);

		struct client_t
		{
			int state;
			char __pad0[0x30 - 0x04];
			netadr_s remoteAddress;
			int qport;
			char __pad1[0x41DF0 - 0x48];
			gentity_s* gentity;
			char __pad2[0x42158 - 0x41DF8];
			int testClient;
			char __pad3[0x11E870 - 0x4215C];
		};
		static_assert(sizeof(client_t) == 0x11E870);
		static_assert(offsetof(client_t, state) == 0x00000);
		static_assert(offsetof(client_t, remoteAddress) == 0x00030);
		static_assert(offsetof(client_t, qport) == 0x00044);
		static_assert(offsetof(client_t, gentity) == 0x41DF0);
		static_assert(offsetof(client_t, testClient) == 0x42158);

		struct XZone
		{
			char __pad0[0x340];
			char name[0x40];
			int flags;
			char __pad1[0x22C];
		};
		static_assert(offsetof(XZone, name) == 0x340);
		static_assert(offsetof(XZone, flags) == 0x380);
		static_assert(sizeof(XZone) == 0x5B0);
	}
#ifdef __cplusplus
}
#endif
