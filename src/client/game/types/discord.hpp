#pragma once

#include <cstddef>
#include <cstdint>

namespace game::native_discord
{
	// MP 19B4A0 / SP 3AB60 serialize this; 1E2180 / 74F10 copy 112 bytes.
	// This predates partyPrivacy in discord-rpc's public headers; adding it
	// would shift all secrets.
	struct discord_presence
	{
		const char* state;
		const char* details;
		std::int64_t start_timestamp;
		std::int64_t end_timestamp;
		const char* large_image;
		const char* large_text;
		const char* small_image;
		const char* small_text;
		const char* party_id;
		int party_size;
		int party_max;
		const char* match_secret;
		const char* join_secret;
		const char* spectate_secret;
		std::int8_t instance;
	};

	static_assert(sizeof(discord_presence) == 112);
	static_assert(offsetof(discord_presence, party_id) == 64);
	static_assert(offsetof(discord_presence, join_secret) == 88);
	static_assert(offsetof(discord_presence, instance) == 104);

	// MP 19A780 / SP 3A030 copy six callback pointers. Preserve the opaque user
	// callbacks: their inline-string payload differs from newer DiscordUser.
	struct discord_handlers
	{
		void (*ready)();
		void (*disconnected)(int, const char*);
		void (*errored)(int, const char*);
		void (*join_game)(const char*);
		void (*spectate_game)(const char*);
		void (*join_request)(const void*);
	};

	static_assert(sizeof(discord_handlers) == 48);
	static_assert(offsetof(discord_handlers, join_game) == 24);
}
