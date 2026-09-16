#include "../src/client/component/dedicated_settings_command.hpp"

#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>

using namespace dedicated_settings::detail;
using words = std::vector<std::string>;

void check(bool condition, const char* name)
{
	if (!condition) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}

int main()
{
	check(tokenize_settings_command(R"(set sv_hostname "My \"Cool\" Server")") ==
		words{"set", "sv_hostname", "My \"Cool\" Server"}, "escaped quoted hostname");
	check(tokenize_settings_command(R"(set g_password "p\"a;ss//word/*x*/\n\\")") ==
		words{"set", "g_password", R"(p"a;ss//word/*x*/\n\")"}, "password syntax, no backslash parity rule");
	check(tokenize_settings_command(R"(set g_password "a\t\n\\path")") ==
		words{"set", "g_password", R"(a\t\n\\path)"}, "other backslashes preserved");
	check(tokenize_settings_command(R"(set x "")") == words{"set", "x", ""}, "empty quoted value");
	check(tokenize_settings_command(R"(set x ab"cd"ef)") == words{"set", "x", "ab\"cd\"ef"},
		"quote inside bare token stays literal");
	check(tokenize_settings_command(R"(set x "a"b)") == words{"set", "x", "a", "b"}, "adjacent quoted and bare tokens");
	check(tokenize_settings_command(R"(set/*before*/ x "My Server" /* after */)") ==
		words{"set", "x", "My Server"}, "block comments separate tokens");
	check(tokenize_settings_command("/* comment */").empty(), "comment only");
	check(tokenize_settings_command("set x yes /* unterminated") == words{"set", "x", "yes"}, "unclosed comment");
	check(tokenize_settings_command("set x a/* outer /* inner */b */") ==
		words{"set", "x", "a", "b", "*/"}, "block comments do not nest");
	check(tokenize_settings_command("set x a//ignored") == words{"set", "x", "a"}, "adjacent line comment");
	check(split_settings_commands("set x 1;set x 2\r\nset x 3") ==
		words{"set x 1", "set x 2", "", "set x 3"}, "semicolon and CRLF");
	check(split_settings_commands("set x \"a\rb\nc\"") == words{"set x \"a", "b", "c\""},
		"CR and LF break quoted commands");
	check(split_settings_commands(R"(set x "a;b";set y 2)") == words{"set x \"a;b\"", "set y 2"}, "quoted semicolon");
	check(split_settings_commands(R"(set x "a\";set y 2)") == words{R"(set x "a\")", "set y 2"},
		"Cbuf counts escaped quote");
	check(split_settings_commands("set x 1 /* comment;set y 2 */") ==
		words{"set x 1 /* comment", "set y 2 */"}, "semicolon in block comment still splits");
	check(split_settings_commands("set x 1 // comment;set y 2") ==
		words{"set x 1 // comment", "set y 2"}, "semicolon in line comment still splits");
	check(split_settings_commands("// \";ignored\nset x 1") == words{"// \";ignored", "set x 1"},
		"quotes in comments affect native splitter");
	const auto multiline = split_settings_commands("set x 1 /* open\nset y 2 */");
	check(multiline.size() == 2 && tokenize_settings_command(multiline[0]) == words{"set", "x", "1"}
		&& tokenize_settings_command(multiline[1]) == words{"set", "y", "2", "*/"},
		"block comment state does not cross native command boundary");
	std::string quoted_raw;
	const std::string hostname = R"(set sv_hostname "My \"Cool\" Server" /* note */)";
	append_instrumented_settings_commands(hostname, quoted_raw, [&](const std::string& command)
	{
		check(tokenize_settings_command(command) == words{"set", "sv_hostname", "My \"Cool\" Server"},
			"production callback sees decoded value");
		quoted_raw += "marker\n";
	});
	check(quoted_raw == hostname + "\nmarker\n", "escaped source and complete comment remain byte identical");

	// Exercise the production rewrite helper, including raw preservation and
	// tokenization of its emitted stream. The callback is a minimal test planner.
	const std::string raw = "set x 1 /* open;set y 2 */\nset z 3 // keep;set w 4";
	std::string rewritten;
	append_instrumented_settings_commands(raw, rewritten, [&](const std::string& command)
	{
		if (!tokenize_settings_command(command).empty()) rewritten += "marker\n";
	});
	check(rewritten == "set x 1 /* open\nmarker\nset y 2 */\nmarker\nset z 3 // keep\nmarker\nset w 4\nmarker\n",
		"raw slices preserved and actions outside comments");
	int markers = 0;
	for (const auto& command : split_settings_commands(rewritten))
		if (tokenize_settings_command(command) == words{"marker"}) ++markers;
	check(markers == 4, "all injected actions reachable");

	// Model engine exec insertion, using the actual production parser/rewrite.
	// Ledger values are captured by the planner but applied only at markers.
	const std::map<std::string, std::string> files{
		{"parent", "set x before;exec child;set x after"},
		{"child", "set x child /* note */;exec grand;set x child_end"},
		{"grand", R"(set x "grand \"quoted\"")"}};
	std::map<std::string, std::string> actions;
	std::vector<std::string> writes;
	std::size_t next = 0;
	const auto instrument = [&](const std::string& input)
	{
		std::string output;
		append_instrumented_settings_commands(input, output, [&](const std::string& command)
		{
			const auto tokens = tokenize_settings_command(command);
			if (tokens.size() == 3 && tokens[0] == "set")
			{
				const auto id = std::to_string(++next);
				actions[id] = tokens[2];
				output += "marker " + id + "\n";
			}
		});
		return split_settings_commands(output);
	};
	const auto initial = instrument(files.at("parent"));
	std::deque<std::string> queue(initial.begin(), initial.end());
	while (!queue.empty())
	{
		const auto tokens = tokenize_settings_command(queue.front());
		queue.pop_front();
		if (tokens.size() == 2 && tokens[0] == "exec")
		{
			const auto child = instrument(files.at(tokens[1]));
			queue.insert(queue.begin(), child.begin(), child.end());
		}
		else if (tokens.size() == 2 && tokens[0] == "marker") writes.push_back(actions.at(tokens[1]));
	}
	check(writes == words{"before", "child", "grand \"quoted\"", "child_end", "after"}, "nested exec action order");
	std::cout << "dedicated settings command tests passed\n";
}
