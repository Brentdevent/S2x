#pragma once

#include "structs.hpp"

#include <utils/nt.hpp>

#include <string_view>

namespace arxan::detail
{
	void set_address_to_call(const void* address);
	extern void* callstack_proxy_addr;
}

namespace game
{
	size_t get_base();
	
	namespace environment
	{
		enum class mode
		{
			singleplayer,
			multiplayer,
			zombies,
		};

		struct online_mode_info
		{
			std::string_view token;
			std::string_view default_gametype;
			int max_players;
			int minimum_direct_players;
			int arena_mode;
		};

		mode get_mode();
		void set_mode(mode mode);

		bool is_dedicated();
		void set_dedicated(bool dedicated);

		bool is_singleplayer();
		bool is_multiplayer();
		bool is_zombies();
		bool uses_multiplayer_binary();

		const online_mode_info& get_online_mode_info();

		std::string get_string();
	}

	bool is_valid_binary();

	inline size_t relocate(const size_t val)
	{
		if (!val) return 0;

		const auto base = get_base();
		return base + val;
	}

	inline size_t derelocate(const size_t val)
	{
		if (!val) return 0;

		const auto base = get_base();
		return val - base;
	}

	inline size_t derelocate(const void* val)
	{
		return derelocate(reinterpret_cast<size_t>(val));
	}

	inline size_t select(const size_t mp_val, const size_t sp_val)
	{
		return relocate(environment::uses_multiplayer_binary() ? mp_val : sp_val);
	}

	inline size_t select(const void* mp_val, const void* sp_val)
	{
		return select(reinterpret_cast<size_t>(mp_val), reinterpret_cast<size_t>(sp_val));
	}

	template <typename T>
	class base_symbol
	{
	public:
		base_symbol(const size_t mp_address)
			: mp_address_(mp_address)
		{
		}

		base_symbol(const size_t mp_address, const size_t sp_address)
			: mp_address_(mp_address)
			, sp_address_(sp_address)
		{
		}

		T* get() const
		{
			return reinterpret_cast<T*>(select(this->mp_address_, this->sp_address_));
		}

		operator T* () const
		{
			return this->get();
		}

		T* operator->() const
		{
			return this->get();
		}

	private:
		size_t mp_address_{};
		size_t sp_address_{};
	};

	template <typename T>
	struct symbol : base_symbol<T>
	{
		using base_symbol<T>::base_symbol;
	};

	template <typename T, typename... Args>
	struct symbol<T(Args...)> : base_symbol<T(Args...)>
	{
		using func_type = T(Args...);

		using base_symbol<func_type>::base_symbol;

		T call_safe(Args... args)
		{
			arxan::detail::set_address_to_call(this->get());
			return static_cast<func_type*>(arxan::detail::callstack_proxy_addr)(args...);
		}
	};

	std::filesystem::path get_appdata_path();

	bool Cbuf_AddText(int localClientNum, const char* text);
	void Cbuf_AddCall(void* function);

	int Cmd_Argc();

	bool is_server_running();
	bool is_local_play();

	bool virtual_lobby_loaded();

	namespace hks
	{
		cclosure* cclosure_Create(lua_function func);
	}
}

inline size_t operator"" _g(const size_t val)
{
	return game::relocate(val);
}

#include "symbols.hpp"
