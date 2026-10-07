#include <std_include.hpp>
#include "launcher.hpp"

#include "resources.hpp"
#include <RmlUi/Core.h>

#include <utils/flags.hpp>
#include <utils/io.hpp>
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
	main_window_("S2x", 880, 420)
{
	this->create_main_menu();
}

void launcher::create_main_menu()
{
	this->document_ = this->main_window_.load_document("launcher.rml");
	this->main_window_.set_event_handler(
		[this](Rml::Event& event)
		{
			this->process_event(event);
		});
	this->document_->GetElementById("licenses-text")
		->SetInnerRML(Rml::StringUtilities::EncodeRml(std::string(launcher_resources::load("licenses.txt"))));
	this->update_options();
}

void launcher::show_menu(const std::string& name)
{
	for (const auto* menu : {"play", "options", "about", "licenses"})
	{
		this->document_->GetElementById("menu-"s + menu)->SetClass("active", name == menu);

		if (auto* link = this->document_->GetElementById("nav-"s + menu))
		{
			link->SetClass("active", name == menu || (name == "licenses" && menu == "about"s));
		}
	}
}

void launcher::update_options()
{
	const auto console = this->launch_options_.console;
	const auto* label = "S2x console (default)";

	if (console == console_mode::terminal)
	{
		label = "Enhanced terminal";
	}
	else if (console == console_mode::disabled)
	{
		label = "No console";
	}

	this->document_->GetElementById("console-label")->SetInnerRML(label);

	for (const auto* name : {"syscon", "terminal", "disabled"})
	{
		this->document_->GetElementById("console-"s + name)
			->SetClass("selected", name == std::string(get_console_mode_name(console)));
	}

	this->document_->GetElementById("no-steam")->SetClass("checked", this->launch_options_.no_steam);
	this->document_->GetElementById("no-update")->SetClass("checked", this->launch_options_.no_update);
}

void launcher::process_event(Rml::Event& event)
{
	auto* dropdown = this->document_->GetElementById("console-mode");
	auto* target = event.GetTargetElement();

	if (event == Rml::EventId::Focus && !dropdown->Contains(target))
	{
		dropdown->SetClass("open", false);
	}

	while (target && !target->HasAttribute("action"))
	{
		target = target->GetParentNode();
	}

	if (event == Rml::EventId::Keydown)
	{
		const auto key = event.GetParameter<int>("key_identifier", 0);

		if (key == Rml::Input::KI_ESCAPE)
		{
			if (dropdown->IsClassSet("open"))
			{
				dropdown->SetClass("open", false);
				this->document_->GetElementById("console-button")->Focus();
			}
			else
			{
				this->main_window_.close();
			}

			event.StopPropagation();
		}
		else if (target && dropdown->Contains(target) && (key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN))
		{
			const char* ids[]{"console-syscon", "console-terminal", "console-disabled"};
			const auto was_open = dropdown->IsClassSet("open");
			auto index = static_cast<int>(this->launch_options_.console);

			for (auto i = 0; i < 3; ++i)
			{
				if (target->GetId() == ids[i])
				{
					index = i;
				}
			}

			if (was_open)
			{
				index = (index + (key == Rml::Input::KI_DOWN ? 1 : 2)) % 3;
			}

			dropdown->SetClass("open", true);
			this->document_->GetContext()->Update();
			this->document_->GetElementById(ids[index])->Focus();
			event.StopPropagation();
		}
		else if (target
			&& (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER || key == Rml::Input::KI_SPACE))
		{
			target->Click();
			event.StopPropagation();
		}

		return;
	}

	if (event != Rml::EventId::Click)
	{
		return;
	}

	const auto action = target ? target->GetAttribute<Rml::String>("action", "") : "";
	const auto was_open = dropdown->IsClassSet("open");
	dropdown->SetClass("open", false);

	if (action == "dropdown")
	{
		dropdown->SetClass("open", !was_open);
	}
	else if (action.starts_with("menu:"))
	{
		this->show_menu(action.substr(5));
	}
	else if (action == "play:singleplayer")
	{
		this->select_mode(game::environment::mode::singleplayer);
	}
	else if (action == "play:multiplayer")
	{
		this->select_mode(game::environment::mode::multiplayer);
	}
	else if (action == "play:zombies")
	{
		this->select_mode(game::environment::mode::zombies);
	}
	else if (action.starts_with("console:") || action == "toggle-steam" || action == "toggle-update")
	{
		if (action.starts_with("console:"))
		{
			const auto console = parse_console_mode(action.substr(8));

			if (!console)
			{
				return;
			}

			this->launch_options_.console = *console;
			this->document_->GetElementById("console-button")->Focus();
		}
		else if (action == "toggle-steam")
		{
			this->launch_options_.no_steam = !this->launch_options_.no_steam;
		}
		else
		{
			this->launch_options_.no_update = !this->launch_options_.no_update;
		}

		this->update_options();
		this->save_launch_options();
	}
	else if (action == "github" || action == "discord")
	{
		// Only these explicit links can leave the embedded interface.
		const auto* url = action == "github" ? "https://github.com/Brentdevent/S2x" : "https://discord.gg/wdC8Jpc2cC";
		ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
	}
}

std::optional<game::environment::mode> launcher::run() const
{
	this->main_window_.run();

	return this->mode_;
}

void launcher::apply_saved_launch_options()
{
	apply_launch_options(load_launch_options());
}

void launcher::select_mode(const game::environment::mode mode)
{
	this->mode_ = mode;
	this->main_window_.close();
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

	if (!console.has_value() || (values[1] != "0" && values[1] != "1") || (values[2] != "0" && values[2] != "1"))
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
	const auto has_explicit_console = utils::flags::has_flag("-noconsole") || utils::flags::has_flag("-terminal")
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
	return utils::string::va("%s|%d|%d", get_console_mode_name(options.console), options.no_steam, options.no_update);
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
