#include <std_include.hpp>
#include <map>
#include "loader/component_loader.hpp"

#include "dedicated_settings.hpp"
#include "dedicated_settings_command.hpp"
#include "dedicated_settings_config.hpp"
#include "dedicated_settings_value.hpp"
#include "dedicated_settings_copy.hpp"

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
//
// Config and console values are recorded when the engine executes them, not
// when the text is read or typed. Tracked configs and console input are
// rewritten on their way into the command buffer: commands are split the way
// the engine splits them, and every dvar write is immediately followed by a
// private "dedicatedSettings _act <id>" command whose payload (source, name,
// value) is held by this component. The engine then runs the actions in its
// own order - nested and repeated execs included - so a parent's later
// command wins over a child's without reconstructing that order from file
// reads, and nothing about a value has to fit the engine's tokenizer.
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

		// One deferred ledger write, executed by "dedicatedSettings _act <id>".
		struct pending_action
		{
			std::string source{};
			std::string name{};
			std::string value{};
			std::chrono::steady_clock::time_point committed{};
			bool forget{};
			// "<dvar> <value>" with no command of that name; whether the first
			// token is a dvar is decided on the engine thread when it runs.
			bool bare{};
			std::optional<detail::copied_assignment> copied{};
		};

		// Never log while holding this: the terminal input path holds its own
		// print lock while the console transform runs, and console output takes
		// that same lock, so logging under the ledger lock would invert them.
		std::mutex mutex;
		std::vector<entry> ledger{};
		detail::admin_config_files admin_exec_files{};
		std::map<std::uint64_t, pending_action> pending{};
		std::atomic<std::uint64_t> next_action_id{1};
		std::atomic_bool typed_fallback_reported{false};

		// Queued command text executes within the frames that follow its
		// submission, so an action still pending an hour after it was committed
		// is taken to belong to text that never ran (a rejected console
		// submission, a level change mid-buffer). That is a heuristic - the engine
		// does not report a cancelled submission - chosen so that reclamation is
		// by age only, never by count, and a hard ceiling only refuses new
		// batches (which are then not executed either).
		constexpr auto pending_action_lifetime = std::chrono::hours{1};
		constexpr std::size_t max_pending_actions = 65536;
		constexpr auto act_action = "_act";

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

		// Values of credential-style dvars (g_password, net_socksPassword, rcon
		// secrets) never reach this component's console output or the log file.
		// The engine's own names are numbers here (g_password is 5370), so both
		// the name as typed and the display name that number resolves to are
		// tested. The raw console echo of the typed line is outside this scope.
		bool is_sensitive(const std::string& name)
		{
			const auto engine_name = game::lookup::dvars::resolve_engine_name(name);
			const auto display_name = game::lookup::dvars::resolve_display_name(engine_name);
			for (const auto candidate : {std::string_view{name}, display_name})
			{
				const auto lowered = utils::string::to_lower(std::string{candidate});
				for (const auto* needle : {"password", "passwd", "secret", "token", "rcon"})
				{
					if (lowered.find(needle) != std::string::npos)
					{
						return true;
					}
				}
			}

			return false;
		}

		const char* display_value(const std::string& name, const std::string& value)
		{
			return is_sensitive(name) ? "<redacted>" : value.data();
		}

		using detail::normalize_exec_name;

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

		// Tokenize one command already separated by the native Cbuf rules.
		std::vector<std::vector<std::string>> tokenize(const std::string& text)
		{
			auto tokens = detail::tokenize_settings_command(text);
			if (tokens.empty())
			{
				return {};
			}

			return {std::move(tokens)};
		}

		// Ledger writes mutate under the lock and log after releasing it.
		void record(const std::string& name, const std::string& value, const std::string& source)
		{
			const auto key = normalize_key(name);
			if (key.empty() || is_lifecycle_managed(key))
			{
				return;
			}

			{
				std::lock_guard _(mutex);
				std::erase_if(ledger, [&key](const entry& existing)
				{
					return existing.key == key;
				});

				ledger.push_back({key, name, value});
			}

			console::info("Dedicated settings: recorded %s \"%s\" (%s).\n", name.data(), display_value(name, value),
				source.data());
		}

		// "reset <dvar>" returns the live dvar to its default; the ledger must
		// forget the value too, or the next restore would bring it back.
		void forget(const std::string& name, const std::string& source)
		{
			const auto key = normalize_key(name);
			std::size_t removed{};
			{
				std::lock_guard _(mutex);
				removed = std::erase_if(ledger, [&key](const entry& existing)
				{
					return existing.key == key;
				});
			}

			if (removed != 0)
			{
				console::info("Dedicated settings: dropped %s after reset (%s).\n", name.data(), source.data());
			}
		}

		bool is_set_command(const std::string& command)
		{
			return command == "set" || command == "seta" || command == "sets" || command == "setu";
		}

		// The result of rewriting one piece of command text. Building it touches
		// neither the ledger nor the tracked-config set (only the id counter
		// advances); the caller commits the actions and child registrations when
		// it hands the text on for execution.
		struct rewrite
		{
			std::string text{};
			std::vector<std::pair<std::uint64_t, pending_action>> actions{};
			std::vector<std::string> children{};
		};

		// The deferred action for one already-split command, if it writes a dvar.
		void plan_command(const std::string& command_text, const std::string& source, rewrite& out)
		{
			for (const auto& tokens : tokenize(command_text))
			{
				const auto command = utils::string::to_lower(tokens[0]);
				pending_action action{};
				action.source = source;

				if (is_set_command(command) && tokens.size() >= 3)
				{
					action.name = tokens[1];
					action.value = join_tokens(tokens, 2);
				}
				else if (command == "reset" && tokens.size() >= 2)
				{
					action.name = tokens[1];
					action.forget = true;
				}
				else if (command == "setfromdvar")
				{
					action.copied = detail::plan_copy(tokens);
					if (!action.copied) continue;
					action.name = action.copied->destination;
				}
				else if (command == "exec")
				{
					if (tokens.size() >= 2)
					{
						out.children.push_back(tokens[1]);
					}

					continue;
				}
				else if (command == "dedicatedsettings")
				{
					continue;
				}
				else if (tokens.size() >= 2)
				{
					// "<dvar> <value>" sets the dvar when no command has that name;
					// the lookup happens on the engine thread when the action runs.
					action.name = tokens[0];
					action.value = join_tokens(tokens, 1);
					action.bare = true;
				}
				else
				{
					continue;
				}

				const auto id = next_action_id.fetch_add(1);
				out.text += utils::string::va("dedicatedSettings %s %llu\n", act_action,
					static_cast<unsigned long long>(id));
				out.actions.emplace_back(id, std::move(action));
			}
		}

		// Keep native command text intact; token comments and Cbuf boundaries
		// are separate stages (even an escaped quote counts for Cbuf).
		rewrite instrument_commands(const std::string& text, const std::string& source)
		{
			rewrite out{};
			out.text.reserve(text.size() * 2);
			detail::append_instrumented_settings_commands(text, out.text, [&](const std::string& command)
			{
				plan_command(command, source, out);
			});
			return out;
		}

		void register_exec_file_locked(const std::string& name, std::vector<std::string>& newly_tracked)
		{
			const auto normalized = normalize_exec_name(name);
			if (admin_exec_files.insert(normalized))
			{
				newly_tracked.push_back(normalized);
			}
		}

		// Publishes a rewrite's actions and child registrations, then logs.
		// Returns false, publishing nothing, when the pending set is at its
		// ceiling even after reclaiming abandoned actions.
		bool commit(rewrite& planned)
		{
			std::vector<std::string> newly_tracked{};
			auto admitted = true;
			{
				std::lock_guard _(mutex);
				const auto now = std::chrono::steady_clock::now();
				std::erase_if(pending, [now](const auto& item)
				{
					return now - item.second.committed > pending_action_lifetime;
				});

				admitted = pending.size() + planned.actions.size() <= max_pending_actions;
				if (admitted)
				{
					for (auto& [id, action] : planned.actions)
					{
						action.committed = now;
						pending.emplace(id, std::move(action));
					}

					for (const auto& child : planned.children)
					{
						register_exec_file_locked(child, newly_tracked);
					}
				}
			}

			if (!admitted)
			{
				console::warn("Dedicated settings: %zu deferred writes are already pending; this text is not tracked.\n",
					max_pending_actions);
				return false;
			}

			for (const auto& name : newly_tracked)
			{
				console::info("Dedicated settings: tracking config '%s'.\n", name.data());
			}

			return true;
		}

		// Tracked configs are rewritten on their way into the exec buffer. A
		// tracked config that cannot be instrumented - its rewrite would not fit
		// the buffer, or the deferred-write ceiling is reached - is not executed
		// at all: running it untracked would leave the ledger holding whatever an
		// earlier config wrote, which is the wrong-order bug this exists to
		// prevent. The warning names the file and the reason so the admin can
		// split it.
		bool instrument_config(const std::string& name, std::string& data, const std::size_t capacity)
		{
			const auto source = normalize_exec_name(name);

			{
				std::lock_guard _(mutex);
				if (!admin_exec_files.contains(source))
				{
					return false;
				}
			}

			auto planned = instrument_commands(data, source);
			if (planned.text.size() >= capacity)
			{
				console::error("Dedicated settings: '%s' was not executed: it is too large to track (%zu of %zu bytes "
					"once instrumented). Split it into smaller configs.\n",
					source.data(), planned.text.size(), capacity);
				data.clear();
				return true;
			}

			if (!commit(planned))
			{
				console::error("Dedicated settings: '%s' was not executed: its writes could not be tracked.\n",
					source.data());
				data.clear();
				return true;
			}

			data = std::move(planned.text);
			return true;
		}

		// Console text is queued for execution, not run at once, so it is
		// rewritten the same way before it is queued. The actions are published
		// before the console hands the text to the command buffer; if that buffer
		// refuses the text, its actions age out unexecuted. Text whose writes
		// cannot be tracked is not executed, the same as a config.
		void instrument_console_input(std::string& text)
		{
			auto planned = instrument_commands(text, "console");
			if (!commit(planned))
			{
				console::error("Dedicated settings: the console command was not executed: its writes could not be tracked.\n");
				text.clear();
				return;
			}

			text = std::move(planned.text);
		}

		// Runs one deferred action on the engine thread.
		void run_action(const std::uint64_t id)
		{
			pending_action action{};
			{
				std::lock_guard _(mutex);
				const auto found = pending.find(id);
				if (found == pending.end())
				{
					return;
				}

				action = std::move(found->second);
				pending.erase(found);
			}

			if (action.copied)
			{
				const auto value = action.copied->read_requested_value([](const std::string& name) -> std::optional<std::string>
				{
					auto* dvar = game::Dvar_FindMalleableVar(name.data());
					if (!dvar) return std::nullopt;
					const auto* current = game::Dvar_ValueToString(dvar, true, &dvar->current);
					if (!current) return std::nullopt;
					return std::string{current};
				});
				if (value) record(action.name, *value, action.source);
				return;
			}

			if (action.bare && !game::Dvar_FindMalleableVar(action.name.data()))
			{
				return;
			}

			if (action.forget)
			{
				forget(action.name, action.source);
			}
			else
			{
				record(action.name, action.value, action.source);
			}
		}

		bool value_matches(game::dvar_t* dvar, const std::string& value)
		{
			if (dvar->type == game::DVAR_TYPE_BOOL)
			{
				const auto parsed = detail::parse_bool_value(value);
				return parsed && dvar->current.enabled == *parsed;
			}

			const auto* current = game::Dvar_ValueToString(dvar, true, &dvar->current);
			return current && value == current;
		}

		void apply_typed(game::dvar_t* dvar, const std::string& value)
		{
			switch (dvar->type)
			{
			case game::DVAR_TYPE_BOOL:
				if (const auto parsed = detail::parse_bool_value(value))
				{
					game::Dvar_SetBool(dvar, *parsed);
				}
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
					name.data(), display_value(name, value));
			}

			apply_typed(dvar, value);
		}
	}

	void register_exec_file(const std::string& name)
	{
		std::vector<std::string> newly_tracked{};
		{
			std::lock_guard _(mutex);
			register_exec_file_locked(name, newly_tracked);
		}

		for (const auto& tracked : newly_tracked)
		{
			console::info("Dedicated settings: tracking config '%s'.\n", tracked.data());
		}
	}

	int restore(const char* reason, const bool startup)
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
				// DVAR_FLAG_WRITE is the engine's DVAR_ROM: never written.
				// DVAR_FLAG_READ is DVAR_INIT: settable from the command line only,
				// which is exactly the startup pass and nothing later.
				if (dvar->flags & game::DVAR_FLAG_WRITE)
				{
					continue;
				}

				if ((dvar->flags & game::DVAR_FLAG_READ) && !startup)
				{
					continue;
				}

				if (value_matches(dvar, item.value))
				{
					continue;
				}

				if (dvar->flags & game::DVAR_FLAG_READ)
				{
					// The engine only takes DVAR_INIT values before registration and
					// fail-fasts on a later typed write. Lift the bit for this single
					// startup write - the value the command line would have latched -
					// and put it back so the dvar stays startup-only afterwards.
					const auto flags = dvar->flags;
					dvar->flags = flags & ~static_cast<unsigned int>(game::DVAR_FLAG_READ);
					apply_value(item.name, item.value);
					dvar->flags = flags;
					++written;
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
				restore("command line", true);
			}, scheduler::pipeline::main);

			filesystem::on_exec_file_transform(instrument_config);
			console::on_input(instrument_console_input);

			command::add("dedicatedSettings", [](const command::params& params)
			{
				const std::string action = params.size() > 1 ? params[1] : "list";

				if (action == act_action)
				{
					// dedicatedSettings _act <id>: the deferred write behind an
					// instrumented command. Typing it by hand runs whatever is pending
					// under that id, or nothing.
					if (params.size() >= 3)
					{
						run_action(std::strtoull(params[2], nullptr, 10));
					}

					return;
				}

				if (action == "restore")
				{
					restore("manual", false);
					return;
				}

				if (action == "clear")
				{
					{
						std::lock_guard _(mutex);
						ledger.clear();
					}

					console::info("Dedicated settings: cleared.\n");
					return;
				}

				std::vector<entry> snapshot{};
				std::size_t tracked{};
				{
					std::lock_guard _(mutex);
					snapshot = ledger;
					tracked = admin_exec_files.size();
				}

				console::info("Dedicated settings: %zu recorded value(s), %zu tracked config(s).\n",
					snapshot.size(), tracked);
				for (const auto& item : snapshot)
				{
					console::info("  %s \"%s\"\n", item.name.data(), display_value(item.name, item.value));
				}
			});
		}
	};
}

REGISTER_COMPONENT(dedicated_settings::component)
