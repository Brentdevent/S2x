#include "flags.hpp"
#include "string.hpp"
#include "nt.hpp"

#include <shellapi.h>
#include <unordered_set>

#include "finally.hpp"

namespace utils::flags
{
	namespace
	{
		std::vector<std::string> parse_arguments()
		{
			int num_args{};
			auto* const argv = CommandLineToArgvW(GetCommandLineW(), &num_args);
			const auto _ = finally([&argv]
			{
				if (argv)
				{
					LocalFree(argv);
				}
			});

			std::vector<std::string> arguments{};

			for (auto i = 0; i < num_args && argv; ++i)
			{
				std::wstring wide_arg(argv[i]);

				if (!wide_arg.empty())
				{
					arguments.emplace_back(string::to_lower(string::convert(wide_arg)));
				}
			}

			return arguments;
		}

		std::unordered_set<std::string> parse_flags()
		{
			std::unordered_set<std::string> flags{};

			for (const auto& arg : parse_arguments())
			{
				if (!arg.empty() && (arg[0] == '-' || arg[0] == '+'))
				{
					flags.emplace(arg);
				}
			}

			return flags;
		}
	}

	bool has_flag(const std::string& flag)
	{
		static const auto enabled_flags = parse_flags();
		return enabled_flags.contains(string::to_lower(flag));
	}

	std::optional<std::string> get_value(const std::string& flag)
	{
		static const auto arguments = parse_arguments();

		const auto wanted_flag = string::to_lower(flag);

		for (auto i = 0ull; i < arguments.size(); ++i)
		{
			if (arguments[i] == wanted_flag)
			{
				const auto value_index = i + 1;

				if (value_index >= arguments.size())
				{
					return std::nullopt;
				}

				const auto& value = arguments[value_index];

				if (!value.empty() && (value[0] == '-' || value[0] == '+'))
				{
					return std::nullopt;
				}

				return value;
			}
		}

		return std::nullopt;
	}
}
