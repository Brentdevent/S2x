#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace game
{
	struct PartyData;
	struct SessionData;
}

namespace party::session
{
	enum class kind
	{
		none,
		dedicated,
		custom,
		headquarters,
		invalid,
	};

	// Stock textual join fields. This is not an engine-facing memory layout.
	struct descriptor
	{
		std::string host_address{};
		std::string key{};
		std::string session_id{};
	};

	struct connect_info
	{
		descriptor session{};
		std::string map_name{};
		std::string gametype{};
		std::uint64_t match_sequence{};
		// Advertised players/slots exclude a dedicated or HQ owner.
		int player_count{};
		int max_players{};
		bool match_running{};
	};

	kind classify(std::string_view value);
	// Response-validation ceiling for this kind in the current game mode.
	int get_player_limit(kind session_kind);
	descriptor describe(const game::SessionData& session);
	bool valid_descriptor(const descriptor& session);

	// Shared owner/capacity policy for a dedicated host or its joined clients.
	bool is_dedicated_lobby(game::PartyData* party_data);
	// Applied dedicated gameplay limit, or -1 without a dedicated lobby.
	int get_dedicated_max_players();
}
