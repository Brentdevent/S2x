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
		enum class platform
		{
			steam,
			microsoft_store,
		};

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

		platform get_platform();
		void set_platform(platform platform);
		bool is_microsoft_store();

		bool is_store_native();
		void set_store_native(bool store_native);
		std::string get_binary_string();

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
	bool is_supported_store_binary_file(const std::filesystem::path& path);

	inline size_t relocate(const size_t val)
	{
		if (!val) return 0;

		const auto base = get_base();
		return base + val;
	}

	inline size_t store_relocate(const size_t val)
	{
		if (!environment::is_store_native())
		{
			throw std::runtime_error("Microsoft Store address used outside the Microsoft Store binary.");
		}

		constexpr size_t store_image_base = 0x140000000;
		return get_base() + (val >= store_image_base ? val - store_image_base : val);
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

	inline size_t select(const size_t steam_val, const size_t store_val)
	{
		return relocate(environment::is_store_native() ? store_val : steam_val);
	}

	inline size_t select(const size_t steam_mp_val, const size_t store_mp_val, const size_t steam_sp_val)
	{
		return environment::uses_multiplayer_binary()
			? select(steam_mp_val, store_mp_val)
			: relocate(steam_sp_val);
	}

	template <typename T>
	class base_symbol
	{
	public:
		base_symbol(const size_t steam_mp_address, const size_t store_mp_address)
			: steam_mp_address_(steam_mp_address)
			, store_mp_address_(store_mp_address)
		{
		}

		base_symbol(const size_t steam_mp_address, const size_t store_mp_address, const size_t steam_sp_address)
			: steam_mp_address_(steam_mp_address)
			, store_mp_address_(store_mp_address)
			, steam_sp_address_(steam_sp_address)
		{
		}

		T* get() const
		{
			return reinterpret_cast<T*>(select(this->steam_mp_address_, this->store_mp_address_, this->steam_sp_address_));
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
		size_t steam_mp_address_{};
		size_t store_mp_address_{};
		size_t steam_sp_address_{};
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

		T call_safe(Args... args) const
		{
			if (environment::is_store_native())
			{
				return this->get()(args...);
			}

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

	const std::byte* CG_GetLocalClientStatic(int localClientNum);
	const std::byte* CL_GetLocalClientActive(int localClientNum);

	namespace hks
	{
		cclosure* cclosure_Create(lua_function func);
	}
}

inline size_t operator"" _ms(const size_t val)
{
	return game::store_relocate(val);
}

#include "symbols.hpp"
