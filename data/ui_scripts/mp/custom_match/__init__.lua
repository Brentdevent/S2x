if game:issingleplayer() or Engine.IsZombiesMode() then
	return
end

CustomMatch = CustomMatch or {}

-- Rank presentation also applies to the in-game scoreboard.
package.loaded["custom_match.ranks"] = nil
require( "custom_match.ranks" )

if not Engine.InFrontend() then
	return
end

package.loaded["custom_match.progression"] = nil
package.loaded["custom_match.setup"] = nil
require( "custom_match.progression" )
require( "custom_match.setup" )
