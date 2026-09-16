// Standalone integration tests: flags.cpp reads the real Windows command line.
#include "../src/common/utils/flags.hpp"

#include <iostream>
#include <map>
#include <stdexcept>

namespace
{
	void check(const bool condition, const char* message)
	{
		if (!condition) throw std::runtime_error(message);
	}
}

int main(const int argc, char** argv)
{
	try
	{
		check(argc >= 2, "missing test scenario (use flags_tests.cmd)");
		const std::string scenario = argv[1];
		using assignments = std::vector<std::pair<std::string, std::string>>;
		using namespace utils::flags;
		if (scenario == "empty" || scenario == "dash")
		{
			check(get_set_values() == assignments{{"sv_hostname", ""}}, "empty assignment lost or next flag consumed");
			check(get_set_value("SV_HOSTNAME") == std::optional<std::string>{""}, "empty is present, not nullopt");
			if (scenario == "dash") check(has_flag("-DEDICATED"), "following dash flag lost");
		}
		else if (scenario == "next")
		{
			check(get_set_values() == assignments{{"sv_hostname", ""}, {"other", "Mixed Case"}}, "empty shifted following +set");
			check(get_set_value("other") == std::optional<std::string>{"mixed case"}, "single accessor must still lowercase");
		}
		else if (scenario == "signed")
		{
			check(get_set_values() == assignments{{"integer", "-1"}, {"decimal", "-0.25"}, {"text", "-DashValue"}}, "dash-prefixed values changed");
			check(get_set_value("integer") == std::optional<std::string>{"-1"}, "negative integer rejected");
			check(get_set_value("decimal") == std::optional<std::string>{"-0.25"}, "negative decimal rejected");
		}
		else if (scenario == "repeated")
		{
			const auto values = get_set_values();
			check(values == assignments{{"sv_hostname", "First Name"}, {"sv_hostname", "SecondName"}, {"sv_hostname", ""}}, "case, order, or duplicate assignments changed");
			check(get_set_value("SV_HOSTNAME") == std::optional<std::string>{"first name"}, "first-match semantics changed");
			std::map<std::string, std::string> settings{{"sv_hostname", "Configured Name"}};
			for (const auto& [name, value] : values) settings[name] = value;
			check(settings.at("sv_hostname").empty(), "last empty override did not clear configured value");
		}
		else if (scenario == "missing")
		{
			check(get_set_values() == assignments{{"valid", "Value"}}, "next command treated as a value");
			check(!get_set_value("missing"), "missing value should be nullopt");
			check(!get_set_value("trailing"), "trailing assignment should be nullopt");
			check(!get_set_value("absent"), "absent assignment should be nullopt");
		}
		else if (scenario == "legacy")
		{
			check(get_value("-LABEL") == std::optional<std::string>{"mixed"}, "get_value lowercase or empty filtering changed");
			check(get_plus_value("MAP") == std::optional<std::string>{"mp_test"}, "get_plus_value changed");
			check(get_plus_value("+MAP") == std::optional<std::string>{"mp_test"}, "explicit plus changed");
			check(!get_value("-number"), "get_value signed-value rejection changed");
			check(!get_plus_value(""), "empty command changed");
		}
		else throw std::runtime_error("unknown test scenario");
		std::cout << "PASS " << scenario << '\n';
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
