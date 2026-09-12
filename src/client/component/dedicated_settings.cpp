#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "dedicated_settings.hpp"

#include "command.hpp"
#include "filesystem.hpp"
#include "scheduler.hpp"
#include "console/console.hpp"

#include "game/game.hpp"
#include "game/lookup/dvars.hpp"

#include <utils/flags.hpp>
#include <utils/string.hpp>

// Dedicated server admin settings ledger.
//
// The stock lobby code restores gameplay defaults (default_xboxlive.cfg,
// playlist rules) around every match start, which wiped values such as
// scr_dom_scorelimit after the first map rotation. The ledger records every
// dvar value that came from the admin (+set launch arguments, the +exec
// config and configs exec'd later, and text typed into the server console)
// and re-applies them right before the game scripts read them.
namespace dedicated_settings
{
	namespace
	{
		struct entry
		{
			std::string key{};
			std::string name{};
			std::string value{};
		};

		std::mutex mutex;
		std::vector<entry> ledger{};
		std::unordered_set<std::string> admin_exec_files{};
		std::atomic_bool typed_fallback_reported{false};

		std::string normalize_key(const std::string& name)
		{
			return utils::string::to_lower(std::string{game::lookup::dvars::resolve_engine_name(name)});
		}

		// Dvars the rotation lifecycle and startup own; recording them would
		// fight the lifecycle or replay one-shot launch state.
		bool is_lifecycle_managed(const std::string& key)
		{
			static const auto managed = []
			{
				std::unordered_set<std::string> keys{};
				for (const auto* name : {
					"mapname", "g_gametype", "ui_mapname", "sv_maxclients", "party_maxPrivatePartyPlayers",
					"pt_pregameStartTimerLength", "pt_gameStartTimerLength", "onlinegame", "systemlink",
					"splitscreen", "sv_running", "dedicated", "net_port", "sv_maprotation",
				})
				{
					keys.emplace(normalize_key(name));
				}

				return keys;
			}();

			return managed.contains(key);
		}

		std::string normalize_exec_name(std::string name)
		{
			std::ranges::replace(name, '\\', '/');
			name = utils::string::to_lower(name);

			if (name.find('.') == std::string::npos)
			{
				name += ".cfg";
			}

			return name;
		}

		std::string join_tokens(const std::vector<std::string>& tokens, const std::size_t first)
		{
			std::string joined{};
			for (auto i = first; i < tokens.size(); ++i)
			{
				if (i > first)
				{
					joined += ' ';
				}

				joined += tokens[i];
			}

			return joined;
		}

		// Splits console text into commands (newline or ';' separated) and
		// tokens (whitespace separated, double quotes group, // comments end
		// the command), mirroring the engine's command tokenizer.
		std::vector<std::vector<std::string>> tokenize(const std::string& text)
		{
			std::vector<std::vector<std::string>> commands{};
			std::vector<std::string> tokens{};
			std::string token{};
			auto in_token = false;
			auto in_quotes = false;

			const auto end_token = [&]
			{
				if (in_token)
				{
					tokens.push_back(token);
					token.clear();
					in_token = false;
				}
			};

			const auto end_command = [&]
			{
				end_token();
				if (!tokens.empty())
				{
					commands.push_back(tokens);
					tokens.clear();
				}
			};

			for (std::size_t i = 0; i < text.size(); ++i)
			{
				const auto character = text[i];

				if (in_quotes)
				{
					if (character == '"')
					{
						in_quotes = false;
						end_token();
					}
					else if (character == '\n')
					{
						in_quotes = false;
						end_command();
					}
					else if (character != '\r')
					{
						token += character;
					}

					continue;
				}

				if (character == '"')
				{
					end_token();
					in_quotes = true;
					in_token = true;
					continue;
				}

				if (character == '\n' || character == '\r' || character == ';')
				{
					end_command();
					continue;
				}

				if (character == '/' && i + 1 < text.size() && text[i + 1] == '/')
				{
					end_command();
					while (i < text.size() && text[i] != '\n')
					{
						++i;
					}

					continue;
				}

				if (std::isspace(static_cast<unsigned char>(character)))
				{
					end_token();
					continue;
				}

				token += character;
				in_token = true;
			}

			end_command();
			return commands;
		}

		void record(const std::string& name, const std::string& value, const char* source)
		{
			const auto key = normalize_key(name);
			if (key.empty() || is_lifecycle_managed(key))
			{
				return;
			}

			std::lock_guard _(mutex);
			std::erase_if(ledger, [&key](const entry& existing)
			{
				return existing.key == key;
			});

			ledger.push_back({key, name, value});
			console::info("Dedicated settings: recorded %s \"%s\" (%s).\n", name.data(), value.data(), source);
		}

		void parse_commands(const std::string& text, const char* source)
		{
			for (const auto& tokens : tokenize(text))
			{
				const auto command = utils::string::to_lower(tokens[0]);

				if (command == "set" || command == "seta" || command == "sets" || command == "setu")
				{
					if (tokens.size() >= 3)
					{
						record(tokens[1], join_tokens(tokens, 2), source);
					}

					continue;
				}

				if (command == "exec")
				{
					if (tokens.size() >= 2)
					{
						register_exec_file(tokens[1]);
					}

					continue;
				}

				// "<dvar> <value>" sets the dvar when no command has that name.
				if (tokens.size() >= 2 && game::Dvar_FindMalleableVar(tokens[0].data()))
				{
					record(tokens[0], join_tokens(tokens, 1), source);
				}
			}
		}

		void on_exec_file_read(const std::string& name, const std::string& data)
		{
			const auto normalized = normalize_exec_name(name);

			{
				std::lock_guard _(mutex);
				if (!admin_exec_files.contains(normalized))
				{
					return;
				}
			}

			parse_commands(data, normalized.data());
		}

		bool value_matches(game::dvar_t* dvar, const std::string& value)
		{
			const auto* current = game::Dvar_ValueToString(dvar, true, &dvar->current);
			return current && value == current;
		}

		void apply_typed(game::dvar_t* dvar, const std::string& value)
		{
			switch (dvar->type)
			{
			case game::DVAR_TYPE_BOOL:
				game::Dvar_SetBool(dvar, value == "1" || utils::string::to_lower(value) == "true");
				break;
			case game::DVAR_TYPE_INT:
				game::Dvar_SetInt(dvar, std::atoi(value.data()));
				break;
			case game::DVAR_TYPE_FLOAT:
				game::Dvar_SetFloat(dvar, static_cast<float>(std::atof(value.data())));
				break;
			case game::DVAR_TYPE_STRING:
				game::Dvar_SetString(dvar, value.data());
				break;
			default:
				break;
			}
		}

		void apply_value(const std::string& name, const std::string& value)
		{
			// Same semantics as the console "set" command: creates unknown dvars,
			// converts the string for the registered type, honours the domain.
			game::Dvar_SetCommand(name.data(), value.data());

			auto* dvar = game::Dvar_FindMalleableVar(name.data());
			if (!dvar || value_matches(dvar, value))
			{
				return;
			}

			if (!typed_fallback_reported.exchange(true))
			{
				console::warn("Dedicated settings: Dvar_SetCommand did not apply %s \"%s\"; using typed setters.\n",
					name.data(), value.data());
			}

			apply_typed(dvar, value);
		}
	}

	void register_exec_file(const std::string& name)
	{
		const auto normalized = normalize_exec_name(name);
		if (normalized.empty())
		{
			return;
		}

		std::lock_guard _(mutex);
		if (admin_exec_files.emplace(normalized).second)
		{
			console::info("Dedicated settings: tracking config '%s'.\n", normalized.data());
		}
	}

	int restore(const char* reason)
	{
		std::vector<entry> snapshot{};
		{
			std::lock_guard _(mutex);
			snapshot = ledger;
		}

		auto written = 0;
		for (const auto& item : snapshot)
		{
			auto* dvar = game::Dvar_FindMalleableVar(item.name.data());
			if (dvar)
			{
				if (dvar->flags & (game::DVAR_FLAG_READ | game::DVAR_FLAG_WRITE))
				{
					continue;
				}

				if (value_matches(dvar, item.value))
				{
					continue;
				}
			}

			apply_value(item.name, item.value);
			++written;
		}

		if (!snapshot.empty())
		{
			console::info("Dedicated settings: restored %d of %zu value(s) (%s).\n",
				written, snapshot.size(), reason);
		}

		return written;
	}

	void log_gametype_values(const std::string& gametype)
	{
		std::string line{};
		for (const auto* suffix : {"scorelimit", "timelimit", "roundlimit", "winlimit"})
		{
			const auto name = utils::string::va("scr_%s_%s", gametype.data(), suffix);
			auto* dvar = game::Dvar_FindMalleableVar(name);
			if (!dvar)
			{
				continue;
			}

			line += utils::string::va(" %s=%s", name, game::Dvar_ValueToString(dvar, true, &dvar->current));
		}

		console::info("Dedicated settings: live%s\n", line.empty() ? " (no scr_ limits registered)" : line.data());
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::is_dedicated())
			{
				return;
			}

			for (const auto& [name, value] : utils::flags::get_set_values())
			{
				record(name, value, "command line");
			}

			// The engine does not apply +set arguments itself under S2x. Apply the
			// recorded values once every component has registered its dvars, so
			// limits such as party_maxplayers are right before the lobby exists.
			scheduler::once([]
			{
				restore("command line");
			}, scheduler::pipeline::main);

			filesystem::on_exec_file_read(on_exec_file_read);

			console::on_input([](const std::string& text)
			{
				parse_commands(text, "console");
			});

			command::add("dedicatedSettings", [](const command::params& params)
			{
				const std::string action = params.size() > 1 ? params[1] : "list";

				if (action == "restore")
				{
					restore("manual");
					return;
				}

				if (action == "clear")
				{
					std::lock_guard _(mutex);
					ledger.clear();
					console::info("Dedicated settings: cleared.\n");
					return;
				}

				std::lock_guard _(mutex);
				console::info("Dedicated settings: %zu recorded value(s), %zu tracked config(s).\n",
					ledger.size(), admin_exec_files.size());
				for (const auto& item : ledger)
				{
					console::info("  %s \"%s\"\n", item.name.data(), item.value.data());
				}
			});
		}
	};
}

REGISTER_COMPONENT(dedicated_settings::component)
