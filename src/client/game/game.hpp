#pragma once

#include "structs.hpp"
#include "launcher/launcher.hpp"	

#include <utils/nt.hpp>

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
		launcher::mode get_mode();
		launcher::mode get_real_mode();

		bool is_sp();
		bool is_mp();
		bool is_zombies();
		bool is_dedi();

		void set_mode(launcher::mode mode);

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
		return relocate(environment::is_mp() ? mp_val : sp_val);
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
}

inline size_t operator"" _g(const size_t val)
{
	return game::relocate(val);
}

#include "symbols.hpp"
