#include "flags.hpp"
#include "string.hpp"
#include "nt.hpp"

#include <shellapi.h>
#include <mutex>
#include <unordered_set>

#include "finally.hpp"

namespace utils::flags
{
	namespace
	{
		std::mutex additional_flags_mutex{};
		std::unordered_set<std::string> additional_flags{};

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

	void add_flag(const std::string& flag)
	{
		const auto normalized_flag = string::to_lower(flag);
		if (normalized_flag.empty() || (normalized_flag[0] != '-' && normalized_flag[0] != '+'))
		{
			return;
		}

		const std::lock_guard lock(additional_flags_mutex);
		additional_flags.emplace(normalized_flag);
	}

	bool has_flag(const std::string& flag)
	{
		static const auto enabled_flags = parse_flags();
		const auto normalized_flag = string::to_lower(flag);

		{
			const std::lock_guard lock(additional_flags_mutex);
			if (additional_flags.contains(normalized_flag))
			{
				return true;
			}
		}

		return enabled_flags.contains(normalized_flag);
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

	std::optional<std::string> get_plus_value(const std::string& command)
	{
		auto flag = string::to_lower(command);
		if (flag.empty())
		{
			return std::nullopt;
		}

		if (flag.front() != '+')
		{
			flag.insert(flag.begin(), '+');
		}

		return get_value(flag);
	}

	std::optional<std::string> get_set_value(const std::string& dvar)
	{
		static const auto arguments = parse_arguments();

		const auto wanted_dvar = string::to_lower(dvar);

		for (auto i = 0ull; i + 2 < arguments.size(); ++i)
		{
			if (arguments[i] != "+set" || arguments[i + 1] != wanted_dvar)
			{
				continue;
			}

			const auto& value = arguments[i + 2];
			if (!value.empty() && (value[0] == '-' || value[0] == '+'))
			{
				return std::nullopt;
			}

			return value;
		}

		return std::nullopt;
	}
}
