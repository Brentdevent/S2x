#include <std_include.hpp>
#include "launcher.hpp"

#include "resource.hpp"

#include <utils/flags.hpp>
#include <utils/io.hpp>
#include <utils/nt.hpp>
#include <utils/string.hpp>

namespace
{
	std::filesystem::path get_launch_options_file()
	{
		return game::get_appdata_path() / "launcher-options.txt";
	}
}

launcher::launcher() :
	launch_options_(load_launch_options()),
	main_window_("S2x Launcher", 1360, 768)
{
	this->create_main_menu();
}

void launcher::create_main_menu()
{
	this->main_window_.get_html_frame()->register_callback(
		"openUrl", [](const std::vector<html_argument>& params) -> CComVariant
		{
			if (params.empty()) return {};

			const auto& param = params[0];
			if (!param.is_string()) return {};

			const auto url = param.get_string();
			ShellExecuteA(nullptr, "open", url.data(), nullptr, nullptr, SW_SHOWNORMAL);

			return {};
		});

	this->main_window_.get_html_frame()->register_callback(
		"selectMode", [this](const std::vector<html_argument>& params) -> CComVariant
		{
			if (params.empty()) return {};

			const auto& param = params[0];
			if (!param.is_number()) return {};

			switch (param.get_number())
			{
			case 1:
				this->select_mode(game::environment::mode::singleplayer);
				break;

			case 2:
				this->select_mode(game::environment::mode::multiplayer);
				break;

			case 3:
				this->select_mode(game::environment::mode::zombies);
				break;

			default:
				return {};
			}

			return {};
		});

	this->main_window_.get_html_frame()->register_callback(
		"getLaunchOptions", [this](const std::vector<html_argument>&) -> CComVariant
		{
			return CComVariant(serialize_launch_options(this->launch_options_).data());
		});

	this->main_window_.get_html_frame()->register_callback(
		"setLaunchOptions", [this](const std::vector<html_argument>& params) -> CComVariant
		{
			if (params.size() != 3 || !params[0].is_string() || !params[1].is_bool() || !params[2].is_bool())
			{
				return {};
			}

			const auto console = parse_console_mode(params[0].get_string());
			if (!console.has_value())
			{
				return {};
			}

			this->launch_options_.console = *console;
			this->launch_options_.no_steam = params[1].get_bool();
			this->launch_options_.no_update = params[2].get_bool();
			this->save_launch_options();

			return {};
		});

	this->main_window_.get_html_frame()->load_html(utils::nt::load_resource(LAUNCHER_MENU));
}

std::optional<game::environment::mode> launcher::run() const
{
	window::run();
	return this->mode_;
}

void launcher::apply_saved_launch_options()
{
	apply_launch_options(load_launch_options());
}

void launcher::select_mode(const game::environment::mode mode)
{
	this->mode_ = mode;
	this->main_window_.get_window()->close();
}

void launcher::save_launch_options() const
{
	utils::io::write_file(get_launch_options_file().wstring(), serialize_launch_options(this->launch_options_));
}

launcher::launch_options launcher::load_launch_options()
{
	launch_options options{};
	std::string stored_options{};
	if (!utils::io::read_file(get_launch_options_file().wstring(), &stored_options))
	{
		return options;
	}

	const auto values = utils::string::split(stored_options, '|');
	if (values.size() != 3)
	{
		return options;
	}

	const auto console = parse_console_mode(values[0]);
	if (!console.has_value() || (values[1] != "0" && values[1] != "1")
		|| (values[2] != "0" && values[2] != "1"))
	{
		return options;
	}

	options.console = *console;
	options.no_steam = values[1] == "1";
	options.no_update = values[2] == "1";
	return options;
}

void launcher::apply_launch_options(const launch_options& options)
{
	const auto has_explicit_console = utils::flags::has_flag("-noconsole")
		|| utils::flags::has_flag("-terminal")
		|| utils::flags::has_flag("-syscon");

	if (!has_explicit_console)
	{
		switch (options.console)
		{
		case console_mode::syscon:
			utils::flags::add_flag("-syscon");
			break;

		case console_mode::terminal:
			utils::flags::add_flag("-terminal");
			break;

		case console_mode::disabled:
			utils::flags::add_flag("-noconsole");
			break;
		}
	}

	if (options.no_steam)
	{
		utils::flags::add_flag("-nosteam");
	}

	if (options.no_update)
	{
		utils::flags::add_flag("-noupdate");
	}
}

std::string launcher::serialize_launch_options(const launch_options& options)
{
	return utils::string::va("%s|%d|%d", get_console_mode_name(options.console), options.no_steam,
		options.no_update);
}

std::optional<launcher::console_mode> launcher::parse_console_mode(const std::string& value)
{
	if (value == "syscon")
	{
		return console_mode::syscon;
	}

	if (value == "terminal")
	{
		return console_mode::terminal;
	}

	if (value == "disabled")
	{
		return console_mode::disabled;
	}

	return std::nullopt;
}

const char* launcher::get_console_mode_name(const console_mode mode)
{
	switch (mode)
	{
	case console_mode::syscon:
		return "syscon";

	case console_mode::terminal:
		return "terminal";

	case console_mode::disabled:
		return "disabled";
	}

	return "syscon";
}

std::string launcher::load_content(const int res)
{
	return utils::nt::load_resource(res);
}
