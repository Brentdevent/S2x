#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console/console.hpp"
#include "game/game.hpp"

#include <utils/hook.hpp>

namespace binding
{
	namespace
	{
		// The native resolver produces 0..108 and 128..143. Keep our marker
		// inside the 144-entry override arrays; never store unbounded custom IDs.
		constexpr int custom_binding = 127;
		constexpr int local_client_count = 2;
		constexpr int key_count = 256;
		constexpr size_t max_command_length = 1024;
		constexpr size_t max_config_length = 8192;
		std::array<std::array<std::string, key_count>, local_client_count> custom_commands{};

		utils::hook::detour bind_hook;
		utils::hook::detour set_binding_hook;
		utils::hook::detour binding_to_string_hook;
		utils::hook::detour get_binding_hook;
		utils::hook::detour unbind_all_hook;
		utils::hook::detour swap_bindings_hook;

		bool valid_key(const int local_client, const int key)
		{
			return local_client >= 0 && local_client < local_client_count && key >= 0 && key < key_count;
		}

		int command_client()
		{
			return game::environment::uses_multiplayer_binary()
				? game::cmd_args->localClientNum[game::cmd_args->nesting] : 0;
		}

		int parse_key(const char* name)
		{
			const auto key = game::Key_StringToKeynum(name);

			// Normalize ASCII letters only: C locale conversion must not change
			// extended key codes (including mouse buttons).
			return key >= 'A' && key <= 'Z' ? key + ('a' - 'A') : key;
		}

		const char* binding_to_string_stub(const int action)
		{
			return action == custom_binding ? "" : binding_to_string_hook.invoke<const char*>(action);
		}

		const char* command_for_key(const int local_client, const int key)
		{
			const auto action = game::playerKeys[local_client].keys[key].binding;
			return action == custom_binding ? custom_commands[local_client][key].c_str()
				: binding_to_string_stub(action);
		}

		bool gamepad_key(const int key)
		{
			return (key >= 1 && key <= 6) || (key >= 14 && key <= 23) || (key >= 28 && key <= 31);
		}

		bool saved_key(const int local_client, const int key)
		{
			const auto action = game::playerKeys[local_client].keys[key].binding;
			return action != 0 && (action == custom_binding || !gamepad_key(key));
		}

		std::string config_line(const int key, const char* text)
		{
			return std::format("bind {} \"{}\"\n", game::Key_KeynumToString(key, 0, 1), text);
		}

		bool fits_config(const int local_client, const int key, const std::string& text)
		{
			// Keep custom additions inside the stock writer's 8 KiB budget,
			// including its generated header and unbindall line.
			auto size = size_t{ 128 } + config_line(key, text.c_str()).size();
			for (auto i = 0; i < key_count; ++i)
			{
				if (i == key || !saved_key(local_client, i))
				{
					continue;
				}

				size += config_line(i, command_for_key(local_client, i)).size();
			}

			return size <= max_config_length;
		}

		bool valid_custom_command(const std::string& text)
		{
			if (text.size() > max_command_length || text.find_first_of("\"\r\n") != std::string::npos)
			{
				console::error("Binding must be at most %zu bytes and contain no embedded quotes or newlines.\n",
					max_command_length);
				return false;
			}

			// A trailing backslash escapes the closing quote in the saved config.
			if (!text.empty() && text.back() == '\\')
			{
				console::error("Binding cannot end with a backslash. Existing binding unchanged.\n");
				return false;
			}

			// Native button actions keep their verified press/release dispatcher.
			// Do not turn unsupported button sequences into permanently held keys.
			bool start = true;
			for (const auto c : text)
			{
				if (c == ';')
				{
					start = true;
					continue;
				}

				if (c == ' ' || c == '\t')
				{
					continue;
				}

				if (start && (c == '+' || c == '-'))
				{
					console::error("Button actions must be bound individually using a native action name.\n");
					return false;
				}

				start = false;
			}

			return true;
		}

		void prepare_rebind(const int local_client, const int key, const int action)
		{
			const auto& state = game::playerKeys[local_client].keys[key];
			if (!state.down || (state.binding != custom_binding && action != custom_binding))
			{
				return;
			}

			// Resolve the effective action, including an active native override.
			const auto previous = get_binding_hook.invoke<int>(local_client, key, 1, nullptr, nullptr);
			if (previous <= 0 || previous >= 109 || !(previous & 1))
			{
				return;
			}

			// Retain down/repeats until the physical release: changing a binding
			// while held must not execute the new command on auto-repeat.
			game::CL_ExecuteKey(local_client, previous + 1, key, 1);
		}

		void set_binding_stub(const int local_client, const int key, const int action)
		{
			if (valid_key(local_client, key))
			{
				prepare_rebind(local_client, key, action);
				custom_commands[local_client][key].clear();
			}

			set_binding_hook.invoke<void>(local_client, key, action);
		}

		void bind_stub()
		{
			const command::params args;
			if (args.size() < 2)
			{
				console::info("bind <key> [command]: set or show a key binding\n");
				return;
			}

			const auto local_client = command_client();
			const auto key = parse_key(args[1]);
			if (!valid_key(local_client, key))
			{
				console::error("Invalid binding key '%s'.\n", args[1]);
				return;
			}

			if (args.size() == 2)
			{
				console::info("%s = \"%s\"\n", game::Key_KeynumToString(key, 0, 1), command_for_key(local_client, key));
				return;
			}

			// Like stock bind, do not change the base layout while an override
			// config is being executed (bit 2). Bit 1 is an active override.
			if (game::playerKeys[local_client].bindingOverrideFlags & 2)
			{
				bind_hook.invoke<void>();
				return;
			}

			const auto text = args.join(2);
			const auto action = game::Key_GetBindingForCmd(text.c_str());
			if (action || text.empty())
			{
				set_binding_stub(local_client, key, action);
				return;
			}

			if (!valid_custom_command(text))
			{
				return;
			}

			if (!fits_config(local_client, key, text))
			{
				console::error("Binding would exceed the key config size limit (%zu bytes).\n", max_config_length);
				return;
			}

			prepare_rebind(local_client, key, custom_binding);
			custom_commands[local_client][key] = text;

			// Bypass our cleanup hook, retaining the native dirty flag and UI notification.
			set_binding_hook.invoke<void>(local_client, key, custom_binding);
		}

		int get_binding_stub(const int local_client, const int key, const int flags, const char** text, int* extra)
		{
			const auto action = get_binding_hook.invoke<int>(local_client, key, flags, text, extra);
			if (action != custom_binding || !text || !valid_key(local_client, key))
			{
				return action;
			}

			*text = custom_commands[local_client][key].c_str();
			return action;
		}

		void unbind_all_stub()
		{
			const auto local_client = command_client();
			if (valid_key(local_client, 0))
			{
				for (auto key = 0; key < key_count; ++key)
				{
					prepare_rebind(local_client, key, 0);
				}

				custom_commands[local_client] = {};
			}

			unbind_all_hook.invoke<void>();
		}

		void swap_bindings_stub()
		{
			const command::params args;
			const auto local_client = command_client();
			if (args.size() != 3 || !valid_key(local_client, 0))
			{
				swap_bindings_hook.invoke<void>();
				return;
			}

			const auto a = parse_key(args[1]);
			const auto b = parse_key(args[2]);
			if (!valid_key(local_client, a) || !valid_key(local_client, b)
				|| (game::playerKeys[local_client].bindingOverrideFlags & 2))
			{
				swap_bindings_hook.invoke<void>();
				return;
			}

			const auto action_a = game::playerKeys[local_client].keys[a].binding;
			const auto action_b = game::playerKeys[local_client].keys[b].binding;
			if (action_a != custom_binding && action_b != custom_binding)
			{
				swap_bindings_hook.invoke<void>();
				return;
			}

			// Stock bind_swap requires both keys to be bound.
			if (!action_a || !action_b || a == b)
			{
				return;
			}

			prepare_rebind(local_client, a, action_b);
			prepare_rebind(local_client, b, action_a);
			std::swap(custom_commands[local_client][a], custom_commands[local_client][b]);
			set_binding_hook.invoke<void>(local_client, a, action_b);
			set_binding_hook.invoke<void>(local_client, b, action_a);
		}

		void execute_key_stub(const int local_client, const int action, const int key, const int release)
		{
			if (action != custom_binding)
			{
				game::CL_ExecuteKey(local_client, action, key, release);
				return;
			}

			if (!valid_key(local_client, key) || release)
			{
				return;
			}

			const auto& state = game::playerKeys[local_client].keys[key];
			if (!state.down || state.repeats != 1 || state.binding != custom_binding)
			{
				return;
			}

			const auto& text = custom_commands[local_client][key];
			if (text.empty())
			{
				return;
			}

			if (!game::Cbuf_AddText(local_client, (text + '\n').c_str()))
			{
				console::error("Command buffer is full; binding was not executed.\n");
			}
		}

		void write_bindings_stub(const int local_client, const std::int64_t file)
		{
			if (!valid_key(local_client, 0))
			{
				return;
			}

			for (auto key = 0; key < key_count; ++key)
			{
				if (!saved_key(local_client, key))
				{
					continue;
				}

				const auto* text = command_for_key(local_client, key);
				if (!text || !*text)
				{
					continue;
				}

				const auto line = config_line(key, text);

				// Stream complete lines through the native file handle, preserving
				// its temporary-file/rename flow without truncating stock bindings.
				game::FS_Printf(file, "%s", line.c_str());
			}
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated())
			{
				return;
			}

			bind_hook.create(game::select(0x45F310, 0x266310), bind_stub);
			set_binding_hook.create(game::select(0x45FBC0, 0x266980), set_binding_stub);
			binding_to_string_hook.create(game::select(0x64B4E0, 0x465A10), binding_to_string_stub);
			get_binding_hook.create(game::select(0x45F720, 0x2664E0), get_binding_stub);
			unbind_all_hook.create(game::select(0x460030, 0x266F00), unbind_all_stub);
			swap_bindings_hook.create(game::select(0x45FE80, 0x266CF0), swap_bindings_stub);

			// Only the accepted key-down call in CL_KeyEvent. Override setup can
			// replay held actions through CL_ExecuteKey; it must not run console commands.
			utils::hook::call(game::select(0x45E41E, 0x265FA1), execute_key_stub);
			utils::hook::call(game::select(0x9DBCA, 0x472DC5), write_bindings_stub);
		}
	};
}

REGISTER_COMPONENT(binding::component)
