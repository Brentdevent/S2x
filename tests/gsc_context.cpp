#include <xsk/gsc/engine/s2.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <utility>

int main()
{
	try
	{
		const xsk::gsc::s2::context context(xsk::gsc::instance::server);

		// These builtins are overridden during client and dedicated-server startup.
		// Their engine IDs come from the S2 mappings used before the #77 update.
		constexpr std::array<std::pair<const char*, std::uint16_t>, 3> required_functions{{
			{"print", 0x00E},
			{"println", 0x00F},
			{"isusingmatchrulesdata", 0x133},
		}};

		bool failed = false;
		for (const auto& [name, expected_id] : required_functions)
		{
			try
			{
				const auto id = context.func_id(name);
				if (id != expected_id)
				{
					std::fprintf(stderr, "%s: expected builtin ID 0x%03X, got 0x%03X\n",
						name, static_cast<unsigned int>(expected_id), static_cast<unsigned int>(id));
					failed = true;
				}
			}
			catch (const std::exception& error)
			{
				std::fprintf(stderr, "%s\n", error.what());
				failed = true;
			}
		}

		if (failed)
		{
			return 1;
		}

		std::puts("S2 startup builtin lookups passed.");
		return 0;
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
