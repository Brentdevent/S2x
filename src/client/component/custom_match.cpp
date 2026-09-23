#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "custom_match.hpp"
#include "game/game.hpp"
#include "game/ui_scripting/execution.hpp"
#include "console/console.hpp"
#include "scheduler.hpp"
#include "ui_scripting.hpp"

#include <utils/finally.hpp>
#include <utils/hook.hpp>
#include <utils/string.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace custom_match
{
	int get_player_limit()
	{
		return game::environment::get_online_mode_info().max_players;
	}

	namespace
	{
		namespace protocol
		{
			// The stock fragment envelope ends with 5 index bits, 5 count bits and
			// 1 format bit. MSG_WriteData byte-aligns the payload, leaving 5 unused bits.
			// Use a versioned marker there, without changing the 22-byte wire header or
			// any recipe/member data. Old hosts have no marker and mean no progression.
			constexpr std::uint8_t marker_mask = 0xF8;
			constexpr std::uint8_t disabled_marker = 0xA8;
			constexpr std::uint8_t enabled_marker = 0xB8;
			constexpr std::string_view command{"0partystate\0", 12};

			std::optional<std::size_t> marker_offset(const std::span<const std::uint8_t> data)
			{
				std::size_t start = 0;
				
				if (data.size() >= 4 && data[0] == 0xFF && data[1] == 0xFF
					&& data[2] == 0xFF && data[3] == 0xFF)
				{
					start = 4; // Receiving messages include the connectionless prefix.
				}

				if (data.size() < start + 18)
				{
					return {};
				}

				for (std::size_t i = 0; i < command.size(); ++i)
				{
					if (data[start + i] != static_cast<std::uint8_t>(command[i]))
					{
						return {};
					}
				}

				const auto bits = data[start + 16] | (data[start + 17] << 8);
				const auto index = bits & 31;
				const auto count = (bits >> 5) & 31;

				if (!(bits & 0x400) || !count || index >= count)
				{
					return {}; // Only the byte-aligned, compressed fragment format.
				}

				return start + 17;
			}

			bool write(const std::span<std::uint8_t> data, const bool enabled)
			{
				const auto offset = marker_offset(data);

				if (!offset)
				{
					return false;
				}

				data[*offset] = (data[*offset] & ~marker_mask)
					| (enabled ? enabled_marker : disabled_marker);
				
				return true;
			}

			std::optional<bool> read(const std::span<const std::uint8_t> data)
			{
				const auto offset = marker_offset(data);

				if (!offset)
				{
					return {};
				}

				switch (data[*offset] & marker_mask)
				{
				case enabled_marker: return true;
				case disabled_marker: return false;
				default: return {};
				}
			}

			bool read_go(const std::string_view value)
			{
				return value == "s2x_progression=1";
			}
		}

		struct session_selection
		{
			const void* party{};
			std::uint64_t session_id{};
			bool progression{};
			std::uint64_t revision{};

			bool matches(const void* identity, const std::uint64_t session) const
			{
				return identity && session && party == identity && session_id == session;
			}

			bool set(const void* identity, const std::uint64_t session, const bool enabled)
			{
				if (!identity || !session || (matches(identity, session) && progression == enabled))
				{
					return false;
				}

				party = identity;
				session_id = session;
				progression = enabled;
				++revision;

				return true;
			}

			bool get(const void* identity, const std::uint64_t session, const bool host, const bool preference)
			{
				if (host && !matches(identity, session))
				{
					set(identity, session, preference);
				}

				return matches(identity, session) && progression;
			}
		};

		game::dvar_t* progression_preference{};
		std::mutex mode_mutex;
		session_selection current_mode{};
		utils::hook::detour start_private_match_hook;

		std::uint64_t get_session_id(game::PartyData* party)
		{
			const auto* session = utils::hook::invoke<game::SessionData*>(0x470F50_g, party);
			return session ? session->sessionId : 0;
		}

		bool is_custom_match(game::PartyData* party)
		{
			// Party_IsRunning also requires local host ownership; clients must use
			// the stock in-party predicate instead. PartyData is reused across lobbies.
			return party && party == game::Lobby_GetPartyData(0) && !game::is_local_play()
				&& utils::hook::invoke<bool>(0x471200_g, party)
				&& game::PartySettings_GetPrivateMatch(&party->settings) != 0
				&& !game::PartySettings_GetRankedMatch(&party->settings);
		}

		bool is_custom_match_host()
		{
			auto* party = game::Lobby_GetPartyData(0);
			return is_custom_match(party) && game::Party_AreWeHost(party);
		}

		void rotation_set_map_stub(game::PartyData* party, const char* map)
		{
			// Stock rotation resolves the current frontend mode's party (HQ in
			// the virtual lobby). The hosted Custom Match belongs to game party 0.
			game::Party_SetMapName(is_custom_match_host() ? game::Lobby_GetPartyData(0) : party, map);
		}

		void rotation_set_settings_map_stub(game::PartySettings* settings, const char* map)
		{
			if (is_custom_match_host())
			{
				settings = &game::Lobby_GetPartyData(0)->settings;
			}

			utils::hook::invoke<void>(0x1973B0_g, settings, map);
		}

		void set_player_limit(game::PartyData* party)
		{
			const auto limit = get_player_limit();
			game::Dvar_SetIntByName("party_maxPrivatePartyPlayers", limit); // stock 5321
			game::Party_SetMaxClients(party, limit);
			auto* private_party = game::Party_GetPrivatePartyData();

			if (private_party && game::Party_AreWeHost(private_party))
			{
				game::Party_SetMaxClients(private_party, limit);
			}
		}

		void apply_player_limit()
		{
			if (is_custom_match_host())
			{
				set_player_limit(game::Lobby_GetPartyData(0));
			}
		}

		void start_private_match_stub()
		{
			// The host session is not active yet. This local host command consumes
			// PartyData's capacity before creating its public/private session slots.
			auto* party = game::Lobby_GetPartyDataFromLocalClient(game::Lobby_GetLocalClientData(0));
			if (party && !game::is_local_play()
				&& game::PartySettings_GetPrivateMatch(&party->settings)
				&& !game::PartySettings_GetRankedMatch(&party->settings))
			{
				set_player_limit(party);
			}

			start_private_match_hook.invoke<void>();
		}

		void sv_register_max_clients_stub(const int minimum)
		{
			utils::hook::invoke<void>(0x6DA7A0_g, minimum);

			// Reapply after registration, before SV_Startup allocates its clients.
			// Dedicated servers own this call site in their separate component.
			if (is_custom_match_host())
			{
				auto* max_clients = game::Dvar_FindMalleableVar("sv_maxclients");
				// SV_Startup reserves the stock 48 slots for the virtual lobby's
				// actors. G_InitGame reads this dvar too; leaving it at 18 crashes
				// ClientConnect when the HQ scene reconnects an actor above slot 17.
				game::Dvar_SetInt(max_clients, *game::virtualLobby_Requested
					? max_clients->reset.integer : get_player_limit());
			}
		}

		void refresh_guest_loadouts(game::PartyData* party, const std::uint64_t revision)
		{
			scheduler::once([party, revision]
			{
				{
					const std::lock_guard lock{mode_mutex};
					if (revision != current_mode.revision || !current_mode.matches(party, get_session_id(party)))
					{
						return;
					}
				}

				if (!game::virtual_lobby_loaded() || !is_custom_match(party) || !*game::hks::lui_lua_state)
				{
					return;
				}

				game::LUI_EnterCriticalSection();

				try
				{
					const auto custom_match_ui = ui_scripting::get_globals().get("CustomMatch");
					if (custom_match_ui.is<ui_scripting::table>())
					{
						const auto refresh = custom_match_ui.as<ui_scripting::table>().get("OnProgressionChanged");
						if (refresh.is<ui_scripting::function>())
						{
							refresh.as<ui_scripting::function>()();
						}
					}
				}
				catch (const std::exception& e)
				{
					console::error("Custom match: loadout refresh failed: %s\n", e.what());
				}

				game::LUI_LeaveCriticalSection();
			}, scheduler::pipeline::main);
		}

		void set_session_mode(game::PartyData* party, const bool enabled, const bool notify_guest = false)
		{
			const auto session_id = get_session_id(party);
			const std::lock_guard lock{mode_mutex};

			if (current_mode.set(party, session_id, enabled) && notify_guest)
			{
				refresh_guest_loadouts(party, current_mode.revision);
			}
		}

		bool has_progression(game::PartyData* party)
		{
			if (!is_custom_match(party))
			{
				return false;
			}

			const auto session_id = get_session_id(party);
			const std::lock_guard lock{mode_mutex};
			// Session creation is asynchronous: the start-private-match command may
			// return before the party is active or has its final session ID. Initialize
			// once the host session is actually usable, not at the command's return.
			return current_mode.get(party, session_id, game::Party_AreWeHost(party),
				progression_preference->current.enabled);
		}

		bool private_loadouts_stub(game::PartyData* party)
		{
			return !has_progression(party) && utils::hook::invoke<bool>(0x4712A0_g, party);
		}

		int overhead_rank_private_match_stub(game::PartyData* party)
		{
			// The character-scene nameplate is rendered natively, independently of
			// the Lua lobby list. Only its rank/icon visibility should treat an
			// enabled custom lobby as public; keep the original integer ABI.
			// Names may come from the social party (slot 2), but progression is
			// always the active custom game session's selection (slot 0).
			return has_progression(game::Lobby_GetPartyData(0))
				? 0 : utils::hook::invoke<int>(0x197150_g, party);
		}

		int progression_private_settings_stub(game::PartySettings* settings)
		{
			auto* party = game::Lobby_GetPartyData(0);
			if (party && settings == &party->settings && has_progression(party))
			{
				return 0;
			}

			return game::PartySettings_GetPrivateMatch(settings);
		}

		bool set_progression(const bool enabled)
		{
			auto* party = game::Lobby_GetPartyData(0);

			if (!is_custom_match(party) || !game::Party_AreWeHost(party)
				|| !game::virtual_lobby_loaded() || (party->hostState & 0xFC) != 4)
			{
				return false;
			}

			game::Dvar_SetBool(progression_preference, enabled);
			set_session_mode(party, enabled);
			// Republish the setting through the stock acknowledged PartyState stream.
			utils::hook::invoke<void>(0x4938E0_g, party);
			return true;
		}

		void* write_party_state_stub(game::msg_t* message, const void* data, const int size)
		{
			auto* result = utils::hook::invoke<void*>(0xDDAF0_g, message, data, size);
			auto* party = game::Lobby_GetPartyData(0);

			if (!message->overflowed && message->data && message->cursize > 0
				&& is_custom_match(party) && game::Party_AreWeHost(party))
			{
				protocol::write({reinterpret_cast<std::uint8_t*>(message->data),
					static_cast<std::size_t>(message->cursize)}, has_progression(party));
			}

			return result;
		}

		bool read_party_state(game::PartyData* party)
		{
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(party);
			const auto count = *reinterpret_cast<const int*>(bytes + 0x186498);
			const auto* fragments = reinterpret_cast<const game::msg_t*>(bytes + 0x186518);

			if (count < 1 || count > 31)
			{
				return false;
			}

			for (int i = 0; i < count; ++i)
			{
				const auto& fragment = fragments[i];
				if (fragment.overflowed || !fragment.data || fragment.cursize < 22
					|| fragment.cursize > fragment.maxsize
					|| protocol::read({reinterpret_cast<const std::uint8_t*>(fragment.data),
						static_cast<std::size_t>(fragment.cursize)}) != true)
				{
					return false; // Legacy, disabled, malformed or mixed fragments fail closed.
				}
			}

			return true;
		}

		char finish_party_state_stub(game::PartyData* party, game::PartyActiveClient* client,
			game::netadr_s* from)
		{
			// This call is reached only after stock host/sequence validation and full
			// recipe/member parsing succeeds. Read before the stock finalizer clears
			// the fragment accumulator and starts stats synchronization.
			if (is_custom_match(party) && !game::Party_AreWeHost(party))
			{
				set_session_mode(party, read_party_state(party), true);
			}

			return utils::hook::invoke<char>(0x4749B0_g, party, client, from);
		}

		const char* format_go_stub(const char* format, const int party_id, const int playlist,
			const int slots, const int private_match, const int flags, const char* map,
			const char* gametype, const unsigned int value)
		{
			const auto* original = utils::string::va(format, party_id, playlist, slots,
				private_match, flags, map, gametype, value);
			auto* party = game::Lobby_GetPartyData(0);

			if (party_id != 0 || !is_custom_match(party) || !game::Party_AreWeHost(party))
			{
				return original;
			}

			// Go can arrive before the last acknowledged PartyState update. Carry the
			// same selection here, before clients choose their in-game stats packets.
			return utils::string::va("%s s2x_progression=%i", original, has_progression(party) ? 1 : 0);
		}

		int validate_go_host_stub(game::PartyData* party, game::netadr_s* from)
		{
			const auto valid = utils::hook::invoke<int>(0x479490_g, party, from);

			if (valid && is_custom_match(party) && !game::Party_AreWeHost(party)
				&& std::string_view(game::Cmd_Argv(3)) == "1")
			{
				set_session_mode(party, game::Cmd_Argc() > 8 && protocol::read_go(game::Cmd_Argv(8)));
			}

			return valid;
		}

		std::vector<std::string> read_tokens(const char* name)
		{
			std::vector<char> buffer(98304);
			const char* cursor = game::DB_ReadRawFile(name, buffer.data(), static_cast<int>(buffer.size()));
			std::vector<std::string> tokens;

			if (!cursor)
			{
				return tokens;
			}

			game::Com_BeginParseSession(name);
			const auto end_parse = utils::finally([] { game::Com_EndParseSession(); });

			while (cursor)
			{
				const std::string token{game::Com_Parse(&cursor)};
				if (!token.empty())
				{
					tokens.push_back(token);
				}
			}

			return tokens;
		}

		ui_scripting::table get_catalog()
		{
			ui_scripting::table catalog, gametypes, maps;

			catalog["gametypes"] = gametypes;
			catalog["maps"] = maps;
			catalog["playerLimit"] = get_player_limit();

			if (!game::environment::is_multiplayer())
			{
				return catalog;
			}

			int index = 0;

			for (auto ref : read_tokens("maps/mp/gametypes/_gametypes.txt"))
			{
				// Same token format as Scr_ParseGameTypeList_FastFile. Unlike the
				// legacy UI cache, do not truncate refs (e.g. dogfight_ffa) to 11 chars.
				ref = ref.substr(0, ref.find(','));
				const auto recipe = "mp/recipes/" + ref + ".recipe";
				const auto* asset = game::DB_FindXAssetHeader(
					game::ASSET_TYPE_RAWFILE, recipe.data(), false).rawfile;
				if (asset && asset->len > 0)
				{
					gametypes[++index] = ref;
				}
			}

			const auto tokens = read_tokens("mp/basemaps.arena");
			index = 0;

			for (std::size_t i = 0; i < tokens.size();)
			{
				if (tokens[i++] != "{")
				{
					break;
				}
				ui_scripting::table map;
				std::string name;
				int pack = -1;
				while (i + 1 < tokens.size() && tokens[i] != "}")
				{
					const auto key = tokens[i++];
					const auto value = tokens[i++];
					map[key] = value;
					if (key == "map") name = value;
					if (key == "mappack") pack = std::atoi(value.data());
				}
				if (i >= tokens.size() || tokens[i++] != "}")
				{
					break;
				}
				// Use the same file/content checks as SV_StartMap and the stock
				// feeder. StreamingInstallIsMapInstalled is a no-op in the PC binary.
				map["available"] = !name.empty() && pack >= 0
					&& game::DB_FastfileExists(name.data(), 0)
					&& (pack == 0 || game::Content_IsPackAvailable(pack + 1));
				maps[++index] = map;
			}

			return catalog;
		}

		void install_lui_functions()
		{
			const auto lobby = ui_scripting::get_globals()["Lobby"].as<ui_scripting::table>();

			lobby["IsCustomMatch"] = [] { return is_custom_match(game::Lobby_GetPartyData(0)); };
			lobby["GetCustomMatchProgression"] = [] { return has_progression(game::Lobby_GetPartyData(0)); };
			lobby["SetCustomMatchProgression"] = set_progression;
			lobby["GetCustomMatchCatalog"] = get_catalog;
			lobby["GetCustomMatchPlayerLimit"] = get_player_limit;
			lobby["ApplyCustomMatchPlayerLimit"] = apply_player_limit;

			// Bind progression and catalog together, then initialize the shared UI
			// before private_lobby validates a saved or newly selected gametype.
			const auto custom = ui_scripting::get_globals().get("CustomMatch");

			if (custom.is<ui_scripting::table>())
			{
				const auto refresh = custom.as<ui_scripting::table>().get("RefreshCatalog");

				if (refresh.is<ui_scripting::function>())
				{
					refresh();
				}
			}
		}
	}

	bool is_valid_gametype(const std::string_view gametype)
	{
		const auto name = utils::string::to_lower(std::string{gametype});

		for (auto ref : read_tokens("maps/mp/gametypes/_gametypes.txt"))
		{
			ref = ref.substr(0, ref.find(','));

			if (utils::string::to_lower(ref) == name)
			{
				return true;
			}
		}

		return false;
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::is_multiplayer() || game::environment::is_dedicated())
			{
				return;
			}

			progression_preference = game::Dvar_RegisterBool(
				"party_customMatchProgression", true, game::DVAR_FLAG_SAVED);

			// LiveStorage_GetLoadoutStatsGroup and LiveStorage_IsUsingOnlineStats.
			// The latter is also GSC's ranking-enabled predicate (_func_3AC).
			utils::hook::call(0x654172_g, private_loadouts_stub);
			utils::hook::call(0x654662_g, private_loadouts_stub);

			// Select the matching stats packet mask, group and byte-range permissions.
			// Do not change the actual private flag: rules, bots and the loading screen
			// must continue to take the custom-match path, not the playlist path.
			utils::hook::call(0x652E60_g, progression_private_settings_stub);
			utils::hook::call(0x652FA6_g, progression_private_settings_stub);
			utils::hook::call(0x6530CE_g, progression_private_settings_stub);

			// CharacterScene's overhead nameplate renderer (0x16E9A0) gates both
			// the rank number and prestige icon on this private-party query. It
			// runs each frame, so host/replicated toggles apply without rebuilding
			// avatars. Retain all stock rank data, positioning and online/ZM gates.
			utils::hook::call(0x16F054_g, overhead_rank_private_match_stub);

			// In-game overhead names use a separate caller (0x3C9B0), which passes
			// PartySettings to 0x197140 before enabling the rank icon/number. Reuse
			// the session-scoped settings override, not the lobby PartyData stub.
			// Keep stock online, entity, spectator, training and rank-data checks.
			utils::hook::call(0x3D2D3_g, progression_private_settings_stub);

			// Extend only the game lobby's existing host-authenticated state/launch
			// messages. Public matchmaking and dedicated-server protocols are unchanged.
			utils::hook::call(0x49043C_g, write_party_state_stub);
			utils::hook::call(0x477AAA_g, finish_party_state_stub);
			utils::hook::call(0x48F92E_g, format_go_stub);
			utils::hook::call(0x472AE5_g, validate_go_host_stub);
			utils::hook::call(0x6DCDE4_g, sv_register_max_clients_stub);

			start_private_match_hook.create(game::CL_Live_StartPrivateMatchHost, start_private_match_stub);

			// Keep the stock rotation cursor, weights and match-end timing. Correct
			// only its map destination, including checkbox add/select/remove paths.
			utils::hook::call(0x924AB6_g, rotation_set_settings_map_stub);
			utils::hook::call(0x924E46_g, rotation_set_settings_map_stub);
			utils::hook::call(0x924C55_g, rotation_set_map_stub);
			utils::hook::call(0x924DE3_g, rotation_set_map_stub);

			ui_scripting::on_start(install_lui_functions);
		}
	};
}

REGISTER_COMPONENT(custom_match::component)
