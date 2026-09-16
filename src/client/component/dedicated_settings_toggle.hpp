#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dedicated_settings::detail
{
	struct toggled_assignment
	{
		std::string target;
		bool explicit_values;

		// Native no-list dispatch (MP 0x66559c). Lists use the native string
		// setter for every type. Do not reproduce value cycling here.
		bool supports_type(const unsigned int type) const
		{
			return explicit_values || type == 0 || type == 1 || type == 5
				|| type == 6 || type == 10 || type == 11 || type == 12;
		}

		template <typename Lookup, typename Format>
		std::optional<std::string> read_result(Lookup&& lookup, Format&& format) const
		{
			auto* dvar = lookup(target);
			if (!dvar || !supports_type(static_cast<unsigned int>(dvar->type))) return std::nullopt;
			// DVAR_LATCH = 2. Native external setters write latched only;
			// protected current storage needs decoding, latched storage does not.
			const bool latched = (dvar->flags & 2u) != 0;
			const auto* value = format(dvar, !latched, latched ? &dvar->latched : &dvar->current);
			if (!value) return std::nullopt;
			return std::string{value};
		}
	};

	inline std::optional<toggled_assignment> plan_toggle(const std::vector<std::string>& tokens)
	{
		if (tokens.size() < 2) return std::nullopt;
		return toggled_assignment{tokens[1], tokens.size() > 2};
	}
}
