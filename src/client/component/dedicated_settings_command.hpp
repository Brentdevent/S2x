#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dedicated_settings::detail
{
	// MP Cbuf: 0x64a320-0x64a357; exec: 0x64afb0-0x64afdd.
	// Count ALL quotes, without interpreting escapes or either comment syntax.
	// CR/LF always end a command; only semicolons depend on quote parity.
	inline std::vector<std::string> split_settings_commands(const std::string_view text)
	{
		std::vector<std::string> commands;
		std::size_t start = 0;
		bool quoted = false;
		for (std::size_t i = 0; i < text.size(); ++i)
		{
			if (text[i] == '"') quoted = !quoted;
			if (text[i] == '\r' || text[i] == '\n' || (text[i] == ';' && !quoted))
			{
				commands.emplace_back(text.substr(start, i - start));
				start = i + 1;
				quoted = false;
			}
		}
		if (start < text.size()) commands.emplace_back(text.substr(start));
		return commands;
	}

	inline std::vector<std::string> tokenize_settings_command(const std::string_view text)
	{
		// Cmd_TokenizeString -> 0x64ba50. Deliberately no C/string escapes:
		// only backslash-quote inside a quoted token is decoded (0x64bb35).
		std::vector<std::string> tokens;
		std::size_t i = 0;
		const auto comment = [&] (const char second)
		{
			return i + 1 < text.size() && text[i] == '/' && text[i + 1] == second;
		};
		const auto whitespace = [](const char c)
		{
			return c == ' ' || (c >= '\x07' && c <= '\r') || (c >= '\x18' && c <= '\x1b');
		};
		while (i < text.size() && text[i] != '\0')
		{
			if (whitespace(text[i])) { ++i; continue; }
			if (comment('/')) break;
			if (comment('*'))
			{
				i += 2;
				while (i < text.size() && text[i] != '\0'
					&& !(text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/')) ++i;
				if (i == text.size() || text[i] == '\0') break;
				i += 2;
				continue;
			}
			std::string token;
			if (text[i] == '"')
			{
				++i;
				while (i < text.size() && text[i] != '\0' && text[i] != '"')
				{
					if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == '"') ++i;
					token += text[i++];
				}
				if (i < text.size() && text[i] == '"') ++i;
			}
			else
			{
				// Quotes inside an unquoted token are literal (0x64bbb3).
				while (i < text.size() && text[i] != '\0' && !whitespace(text[i])
					&& !comment('/') && !comment('*')) token += text[i++];
			}
			tokens.push_back(std::move(token));
		}
		return tokens;
	}

	template <typename PlanCommand>
	void append_instrumented_settings_commands(const std::string_view text, std::string& output,
		PlanCommand plan_command)
	{
		for (const auto& command : split_settings_commands(text))
		{
			output += command;
			output += '\n';
			// Native tokenization is per command: even an unterminated comment
			// ends at this boundary. Never inject into or strip a raw comment.
			plan_command(command);
		}
	}
}
