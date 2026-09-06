#include <std_include.hpp>

#include "dvars.hpp"
#include "lookup.hpp"

namespace game::lookup::dvars
{
	namespace
	{
		const detail::string_map& dvar_engine_names()
		{
			static const detail::string_map values
			{
				{ "cl_paused", "183" },
				{ "sv_allowAimAssist", "387" },
				{ "net_socksEnabled", "464" },
				{ "net_socksPassword", "542"},
				{ "net_noudp", "707" },
				{ "ui_mapname", "864" },
				{ "pt_gameStartTimerLength", "901" },
				{ "com_sv_running", "1080" },
				{ "playlistFilename", "1369" },
				{ "playlistAggrFilename", "1415" },
				{ "fs_debug", "1467" },
				{ "playListUpdateCheckMinutes", "1580" },
				{ "dedicated_dhclient", "1591" },
				{ "lui_memErrorsFatal", "1626" },
				{ "mapname", "1673" },
				{ "fs_game", "1751" },
				{ "g_gametype", "1924" },
				{ "com_errorMessage", "1943" },
				{ "g_hardcore", "2043" },
				{ "onlinegame", "2291" },
				{ "sv_maxclients", "2299" },
				{ "fs_basegame", "2796" },
				{ "bot_AutoConnectDefault", "2806"},
				{ "cg_fovScale", "3078" },
				{ "fs_ignoreLocalized", "3139" },
				{ "fs_basepath_output", "3510" },
				{ "cl_motdString", "3680" },
				{ "net_socksPort", "3842"},
				{ "net_socksUsername", "3968"},
				{ "fs_homepath", "4068" },
				{ "pt_pregameStartTimerLength", "4853" },
				{ "fs_basepath", "4972" },
				{ "bot_DifficultyDefault", "5046"},
				{ "systemlink", "5075" },
				{ "sv_paused", "5351" },
				{ "com_completionResolveCommand", "5369" },
				{ "net_socksServer", "5377"},
			};

			return values;
		}

		const detail::string_map& dvar_display_names()
		{
			static const detail::string_map values
			{
				{ "183", "cl_paused" },
				{ "387", "sv_allowAimAssist" },
				{ "464", "net_socksEnabled" },
				{ "542", "net_socksPassword" },
				{ "707", "net_noudp" },
				{ "864", "ui_mapname" },
				{ "901", "pt_gameStartTimerLength" },
				{ "1080", "com_sv_running" },
				{ "1369", "playlistFilename" },
				{ "1415", "playlistAggrFilename" },
				{ "1467", "fs_debug" },
				{ "1580", "playListUpdateCheckMinutes" },
				{ "1591", "dedicated_dhclient" },
				{ "1626", "lui_memErrorsFatal" },
				{ "1673", "mapname" },
				{ "1751", "fs_game" },
				{ "1924", "g_gametype" },
				{ "1943", "com_errorMessage" },
				{ "2043", "g_hardcore" },
				{ "2291", "onlinegame" },
				{ "2299", "sv_maxclients" },
				{ "2796", "fs_basegame" },
				{ "2806", "bot_AutoConnectDefault" },
				{ "3078", "cg_fovScale" },
				{ "3139", "fs_ignoreLocalized" },
				{ "3510", "fs_basepath_output" },
				{ "3680", "cl_motdString" },
				{ "3842", "net_socksPort" },
				{ "3968", "net_socksUsername" },
				{ "4068", "fs_homepath" },
				{ "4853", "pt_pregameStartTimerLength" },
				{ "4972", "fs_basepath" },
				{ "5046", "bot_DifficultyDefault" },
				{ "5075", "systemlink" },
				{ "5351", "sv_paused" },
				{ "5369", "com_completionResolveCommand" },
				{ "5377", "net_socksServer" },
			};

			return values;
		}
	}

	std::string_view resolve_engine_name(const std::string_view name)
	{
		return detail::find_or_self(dvar_engine_names(), name);
	}

	std::string_view resolve_display_name(const std::string_view name)
	{
		return detail::find_or_self(dvar_display_names(), name);
	}
}
