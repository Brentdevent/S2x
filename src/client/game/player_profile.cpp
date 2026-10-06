#include <std_include.hpp>
#include "player_profile.hpp"
#include "game.hpp"

#include <utils/flags.hpp>
#include <utils/io.hpp>
#include <utils/nt.hpp>
#include <utils/string.hpp>

namespace player_profile
{
	namespace
	{
		constexpr auto selection_file = "players2/profile.txt";

		std::string normalize_name(const std::string& name)
		{
			const auto result = utils::string::to_lower(name);
			const auto alphanumeric = [](const char c)
			{
				return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
			};
			if (result.empty() || result.size() > 32 || !alphanumeric(result.front())
				|| !std::all_of(result.begin(), result.end(), [&](const char c)
				{
					return alphanumeric(c) || c == '_' || c == '-';
				}))
			{
				throw std::runtime_error("Use 1-32 letters, numbers, underscores or hyphens; start with a letter or number.");
			}

			if (result == "con" || result == "prn" || result == "aux" || result == "nul"
				|| (result.size() == 4 && (result.starts_with("com") || result.starts_with("lpt"))
					&& result.back() >= '1' && result.back() <= '9'))
			{
				throw std::runtime_error("That profile name is reserved by Windows.");
			}

			return result;
		}

		std::filesystem::path directory_for(const std::string& name)
		{
			return name == "default" ? std::filesystem::path{"players2/user"}
				: std::filesystem::path{"players2/profiles"} / name / "user";
		}

		std::string load_selection()
		{
			if (game::environment::is_dedicated())
			{
				return "default";
			}

			if (utils::flags::has_flag("-profile"))
			{
				const auto value = utils::flags::get_value("-profile");
				if (!value)
				{
					throw std::runtime_error("Usage: s2x.exe -profile <name>");
				}

				return normalize_name(*value);
			}

			const auto saved = utils::io::read_file(selection_file);
			return saved.empty() ? "default" : normalize_name(saved);
		}

		struct profile
		{
			std::string name{load_selection()};
			std::filesystem::path directory{directory_for(name)};
			utils::nt::handle<> reservation;
		};

		profile& selected()
		{
			static profile value;
			return value;
		}

		void check_selection_allowed()
		{
			if (utils::flags::has_flag("-profile"))
			{
				throw std::runtime_error("Remove -profile from the command line to select a profile in the launcher.");
			}

			if (selected().reservation)
			{
				throw std::runtime_error("Restart S2x to switch profiles.");
			}
		}
	}

	const std::string& name()
	{
		return selected().name;
	}

	const std::filesystem::path& user_directory()
	{
		reserve();
		return selected().directory;
	}

	void reserve()
	{
		auto& value = selected();
		if (game::environment::is_dedicated() || value.reservation)
		{
			return;
		}

		const auto mutex_name = value.name == "default" ? "s2x_mutex" : "s2x_profile_" + value.name;
		utils::nt::handle<> reservation{CreateMutexA(nullptr, FALSE, mutex_name.c_str())};
		const auto error = GetLastError();
		if (!reservation)
		{
			throw std::runtime_error("Unable to reserve the S2x player profile.");
		}

		if (error == ERROR_ALREADY_EXISTS)
		{
			throw std::runtime_error("Profile '" + value.name
				+ "' is already in use. Choose another profile in the launcher or use -profile <name>.");
		}

		std::filesystem::create_directories(value.directory);
		value.reservation = std::move(reservation);
	}

	std::vector<std::string> list()
	{
		std::vector<std::string> result{"default"};
		const std::filesystem::path root{"players2/profiles"};
		if (std::filesystem::is_directory(root))
		{
			for (const auto& entry : std::filesystem::directory_iterator(root))
			{
				if (!entry.is_directory())
				{
					continue;
				}

				try
				{
					result.push_back(normalize_name(entry.path().filename().string()));
				}
				catch (const std::runtime_error&)
				{
					// Ignore directories that are not valid profile names.
				}
			}
		}

		result.push_back(name());
		std::sort(result.begin() + 1, result.end());
		result.erase(std::remove(result.begin() + 1, result.end(), "default"), result.end());
		result.erase(std::unique(result.begin(), result.end()), result.end());
		return result;
	}

	void select(const std::string& name)
	{
		check_selection_allowed();
		const auto normalized = normalize_name(name);
		const auto directory = directory_for(normalized);
		if (normalized != "default" && !std::filesystem::is_directory(directory))
		{
			throw std::runtime_error("That profile does not exist. Create it first.");
		}

		std::filesystem::create_directories("players2");
		std::ofstream stream(selection_file, std::ios::binary | std::ios::trunc);
		stream << normalized;
		stream.close();
		if (!stream)
		{
			throw std::runtime_error("Unable to save the selected profile.");
		}

		selected().name = normalized;
		selected().directory = directory;
	}

	void create(const std::string& name)
	{
		check_selection_allowed();
		const auto normalized = normalize_name(name);
		const auto directory = directory_for(normalized);
		if (normalized == "default" || std::filesystem::exists(directory.parent_path()))
		{
			throw std::runtime_error("That profile already exists. Select it from the list.");
		}

		std::filesystem::create_directories(directory);
		select(normalized);
	}

	std::string relaunch_arguments()
	{
		// Pin the selection across updater/mode relaunches even if another launcher
		// changes the saved choice. Explicit command-line arguments are already retained.
		return game::environment::is_dedicated() || utils::flags::has_flag("-profile")
			? std::string{} : "-profile " + name();
	}
}
