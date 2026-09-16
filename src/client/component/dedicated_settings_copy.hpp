#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dedicated_settings::detail
{
	struct copied_assignment
	{
		std::string destination;
		std::string source;

		// Called by the deferred action, after the engine has executed the copy.
		// Capture the source's current string, which is the argument passed to
		// Dvar_SetCommand. Reading the destination would lose latched writes.
		// Like ordinary set, the ledger records the requested value; it does
		// not claim that a domain-rejected write was accepted by the engine.
		template <typename Lookup>
		std::optional<std::string> read_requested_value(Lookup&& lookup) const
		{
			return lookup(source);
		}
	};

	inline std::optional<copied_assignment> plan_copy(const std::vector<std::string>& tokens)
	{
		if (tokens.size() != 3) return std::nullopt;
		return copied_assignment{tokens[1], tokens[2]};
	}
}
