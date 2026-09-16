#include "../src/client/component/dedicated_settings_copy.hpp"
#include <iostream>
#include <map>
#include <stdexcept>

void require(const bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

int main()
{
	try
	{
		using dedicated_settings::detail::plan_copy;
		const auto action = plan_copy({"setfromdvar", "scr_dom_scorelimit", "saved_limit"});
		require(action.has_value(), "valid copy must create a deferred action");
		std::map<std::string, std::string> live{{"scr_dom_scorelimit", "100"}};
		const auto lookup = [&](const std::string& name) -> std::optional<std::string>
		{
			const auto found = live.find(name);
			return found == live.end() ? std::nullopt : std::optional<std::string>{found->second};
		};
		require(!action->read_requested_value(lookup), "missing source must not record the old destination");
		// A preceding config command creates the source after planning, then the
		// native setfromdvar applies it before the deferred action reads it.
		live["saved_limit"] = "250";
		live["scr_dom_scorelimit"] = live["saved_limit"];
		const auto recorded = action->read_requested_value(lookup);
		require(recorded == std::optional<std::string>{"250"}, "must capture after engine execution");
		live["scr_dom_scorelimit"] = "100"; // rotation defaults
		live[action->destination] = *recorded; // ledger restoration
		require(live["scr_dom_scorelimit"] == "250", "copied override must survive restoration");
		live["saved_limit"] = "01";
		live["scr_dom_scorelimit"] = "1";
		require(action->read_requested_value(lookup) == std::optional<std::string>{"01"}, "record the native command argument");
		live["saved_limit"] = "250";
		live["scr_dom_scorelimit"] = "100";
		const std::string latched = "250";
		require(action->read_requested_value(lookup) == std::optional<std::string>{latched},
			"a latched copy must not record the old current value");
		live["saved_limit"] = "999999"; // domain rejection leaves destination unchanged
		require(action->read_requested_value(lookup) == std::optional<std::string>{"999999"},
			"copy must follow ordinary set's requested-value policy, not capture stale current");
		live["saved_limit"] = "";
		live["scr_dom_scorelimit"] = "";
		require(action->read_requested_value(lookup) == std::optional<std::string>{""}, "empty copied value is still present");
		live.erase("scr_dom_scorelimit");
		require(action->read_requested_value(lookup) == std::optional<std::string>{""},
			"native copy can create a previously missing destination");
		require(!plan_copy({"setfromdvar", "target"}), "incomplete command must not create an action");
		require(!plan_copy({"setfromdvar", "target", "source", "extra"}), "invalid argument count must not create an action");
		std::cout << "dedicated settings copy tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
