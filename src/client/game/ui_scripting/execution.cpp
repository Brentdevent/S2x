#include <std_include.hpp>
#include "execution.hpp"
#include "component/ui_scripting.hpp"
#include "component/console/console.hpp"

#include <utils/string.hpp>
#include <utils/finally.hpp>

namespace ui_scripting
{
	namespace
	{
		script_value get_field(void* ptr, game::hks::HksObjectType type, const script_value& key)
		{
			const auto state = *game::hks::lui_lua_state;
			const auto top = state->m_apistack.top - state->m_apistack.bottom;
			const auto restore_stack = utils::finally([=]()
			{
				state->m_apistack.top = state->m_apistack.bottom + top;
			});

			reserve_stack(state, 2);

			push_value(key);

			game::hks::HksObject value{};
			game::hks::HksObject obj{};
			obj.t = type;
			obj.v.ptr = ptr;

			game::hks::hks_obj_gettable(&value, state, &obj, &state->m_apistack.top[-1]);
			return value;
		}

		void set_field(void* ptr, game::hks::HksObjectType type, const script_value& key, const script_value& value)
		{
			const auto state = *game::hks::lui_lua_state;

			game::hks::HksObject obj{};
			obj.t = type;
			obj.v.ptr = ptr;

			game::hks::hks_obj_settable(state, &obj, &key.get_raw(), &value.get_raw());
		}
	}

	void reserve_stack(game::hks::lua_State* state, const int count)
	{
		if (state->m_apistack.alloc_top - state->m_apistack.top < count)
		{
			game::hks::grow_api_stack(&state->m_callStack, state, count, false);
		}
	}

	void push_value(game::hks::lua_State* state, const game::hks::HksObject value)
	{
		reserve_stack(state, 1);
		*state->m_apistack.top++ = value;
	}

	void push_value(const script_value& value)
	{
		push_value(*game::hks::lui_lua_state, value.get_raw());
	}

	void push_value(const game::hks::HksObject& value)
	{
		push_value(*game::hks::lui_lua_state, value);
	}

	script_value get_return_value(int offset)
	{
		const auto state = *game::hks::lui_lua_state;
		return state->m_apistack.top[-1 - offset];
	}

	arguments get_return_values(game::hks::lua_State* state, const std::ptrdiff_t base)
	{
		const auto count = state->m_apistack.top - state->m_apistack.bottom - base;
		arguments values;

		for (std::ptrdiff_t i = 0; i < count; ++i)
		{
			values.emplace_back(state->m_apistack.bottom[base + i]);
		}

		if (values.empty())
		{
			values.push_back({});
		}

		return values;
	}

	arguments get_return_values(game::hks::lua_State* state)
	{
		return get_return_values(state, state->m_apistack.base - state->m_apistack.bottom);
	}

	arguments get_return_values()
	{
		return get_return_values(*game::hks::lui_lua_state);
	}

	bool notify(const std::string& name, const event_arguments& arguments)
	{
		const auto state = *game::hks::lui_lua_state;
		if (state == nullptr)
		{
			return false;
		}

		const auto _1 = utils::finally(game::LUI_LeaveCriticalSection);
		game::LUI_EnterCriticalSection();

		try
		{
			const auto engine = get_globals().get("Engine").as<table>();
			const auto root = engine.get("GetLuiRoot")()[0].as<userdata>();
			const auto process_event = root.get("processEvent");

			table event{};
			event.set("name", name);
			event.set("dispatchChildren", true);

			for (const auto& arg : arguments)
			{
				event.set(arg.first, arg.second);
			}

			process_event(root, event);
			return true;
		}
		catch (const std::exception& e)
		{
			console::error("Error processing event '%s' %s\n", name.data(), e.what());
		}

		return false;
	}

	arguments call_script_function(const function& function, const arguments& arguments)
	{
		const auto state = *game::hks::lui_lua_state;
		const auto top = state->m_apistack.top - state->m_apistack.bottom;
		const auto restore_stack = utils::finally([=]()
		{
			state->m_apistack.top = state->m_apistack.bottom + top;
		});

		push_value(function);

		for (const auto& argument : arguments)
		{
			push_value(argument);
		}

		if (game::hks::hksi_lua_pcall(state, static_cast<int>(arguments.size()), -1, 0) != 0)
		{
			const auto& error = state->m_apistack.top[-1];
			throw std::runtime_error(error.t == game::hks::TSTRING
				? error.v.str->m_data : "LUI function failed with a non-string error");
		}

		return get_return_values(state, top);
	}

	script_value get_field(const userdata& self, const script_value& key)
	{
		return get_field(self.ptr, game::hks::TUSERDATA, key);
	}

	script_value get_field(const table& self, const script_value& key)
	{
		return get_field(self.ptr, game::hks::TTABLE, key);
	}

	void set_field(const userdata& self, const script_value& key, const script_value& value)
	{
		set_field(self.ptr, game::hks::TUSERDATA, key, value);
	}

	void set_field(const table& self, const script_value& key, const script_value& value)
	{
		set_field(self.ptr, game::hks::TTABLE, key, value);
	}
}
