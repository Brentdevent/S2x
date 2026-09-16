#pragma once

namespace dedicated_settings::detail
{
	// The existing console formatter has a 4096-byte buffer. Bound every
	// externally supplied text field here; ledger values themselves stay intact.
	inline constexpr auto recorded_format = "Dedicated settings: recorded %.256s \"%.256s\" (%.256s).\n";
	inline constexpr auto dropped_format = "Dedicated settings: dropped %.256s after reset (%.256s).\n";
	inline constexpr auto tracking_format = "Dedicated settings: tracking config '%.256s'.\n";
	inline constexpr auto capacity_format = "Dedicated settings: '%.256s' was not executed: it is too large to track (%zu of %zu bytes once instrumented). Split it into smaller configs.\n";
	inline constexpr auto refused_format = "Dedicated settings: '%.256s' was not executed: its writes could not be tracked.\n";
	inline constexpr auto fallback_format = "Dedicated settings: Dvar_SetCommand did not apply %.256s \"%.256s\"; using typed setters.\n";
	inline constexpr auto restored_format = "Dedicated settings: restored %d of %zu value(s) (%.256s).\n";
	inline constexpr auto live_format = "Dedicated settings: live%.3000s\n";
	inline constexpr auto listed_format = "  %.256s \"%.256s\"\n";
}
