#include "../src/client/component/dedicated_settings_command.hpp"
#include "../src/client/component/dedicated_settings_toggle.hpp"
#include <iostream>
#include <map>
#include <stdexcept>

void require(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

struct fake_dvar
{
	unsigned int type = 5;
	unsigned int flags = 0;
	std::string current = "100";
	std::string latched = "100";
};

int main()
{
	try
	{
		using namespace dedicated_settings::detail;
		for (const auto* command : {"toggle", "togglep"})
		{
			std::map<std::string, fake_dvar> live{{"score", {}}};
			const auto lookup = [&](const std::string& name) -> fake_dvar*
			{
				const auto found = live.find(name);
				return found == live.end() ? nullptr : &found->second;
			};
			const auto format = [](fake_dvar* dvar, bool decode, std::string* value)
			{
				require(decode == (value == &dvar->current), "decode must match selected storage");
				return value->c_str();
			};
			const auto action = plan_toggle(tokenize_settings_command(std::string{command} + " score 100 250"));
			require(action.has_value(), "explicit native toggle must be planned");
			std::string ledger = "100";
			live["score"].current = "250"; // native result, not a toggle emulator
			ledger = *action->read_result(lookup, format);
			require(ledger == "250", "set 100; toggle 100 250 must replace ledger 100");
			live["score"].current = "100"; // rotation defaults
			live["score"].current = ledger;
			require(live["score"].current == "250", "recorded toggle must survive restore");
			for (const auto type : {0u, 1u, 5u, 6u, 10u, 11u, 12u})
			{
				const auto ordinary = plan_toggle({command, "score"});
				live["score"] = {type, 0, "1", "0"};
				require(ordinary->read_result(lookup, format) == "1", "ordinary toggle reads current");
				live["score"] = {type, 2, "100", "250"};
				require(ordinary->read_result(lookup, format) == "250", "latched toggle must not decode stale current");
			}
			live["score"] = {7, 0, "", "old"};
			require(action->read_result(lookup, format) == "", "empty native result is present");
			require(!plan_toggle({command, "score"})->read_result(lookup, format), "native rejects no-list string toggle");
			for (const auto type : {2u, 3u, 4u, 8u, 9u})
				require(!plan_toggle({command, "score"})->supports_type(type), "unsupported no-list type");
			live["score"] = {5, 0x800, "100", "100"};
			require(action->read_result(lookup, format) == "100", "rejected write captures unchanged result, not candidate 250");
			live["score"] = {5, 2, "100", "200"};
			require(action->read_result(lookup, format) == "200", "domain rejection preserves previously latched result");
			require(!action->read_result(lookup, [](auto*, bool, auto*) -> const char* { return nullptr; }), "failed formatting must not record");
			live.clear();
			require(!action->read_result(lookup, format), "missing target must not create ledger entry");
			require(!plan_toggle({command}), "missing target argument must not plan");
			require(plan_toggle({command, "score", "250"}).has_value(), "one explicit value is valid");
		}
		std::cout << "dedicated settings toggle tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
