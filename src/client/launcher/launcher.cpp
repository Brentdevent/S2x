#include <std_include.hpp>
#include "launcher.hpp"

#include "resource.hpp"

#include <utils/nt.hpp>

launcher::launcher() :
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

			const auto number = static_cast<mode>(param.get_number());
			this->select_mode(number);

			return {};
		});

	this->main_window_.get_html_frame()->load_html(utils::nt::load_resource(LAUNCHER_MENU));
}

launcher::mode launcher::run() const
{
	window::run();
	return this->mode_;
}

void launcher::select_mode(const mode mode)
{
	this->mode_ = mode;
	this->main_window_.get_window()->close();
}

std::string launcher::load_content(const int res)
{
	return utils::nt::load_resource(res);
}
