#include <std_include.hpp>
#include "functions.hpp"

#include <utils/string.hpp>

#include "component/gsc/script_extension.hpp"
#include "component/gsc/script_loading.hpp"

namespace scripting
{
	namespace
	{
		int find_function_index(const std::string& name, [[maybe_unused]] const bool prefer_global)
		{
			const auto target = utils::string::to_lower(name);

			const auto& functions = gsc::gsc_ctx->func_map();
			const auto& methods = gsc::gsc_ctx->meth_map();

			if (const auto itr = functions.find(target); itr != functions.end())
			{
				return static_cast<int>(itr->second);
			}

			if (const auto itr = methods.find(target); itr != methods.end())
			{
				return static_cast<int>(itr->second);
			}

			return -1;
		}
	}

	std::uint32_t parse_token_id(const std::string& name)
	{
		if (name.starts_with("_ID"))
		{
			return static_cast<std::uint32_t>(std::strtol(name.substr(3).data(), nullptr, 10));
		}

		return 0;
	}

	std::string find_token(std::uint32_t id)
	{
		return gsc::gsc_ctx->token_name(id);
	}

	std::string find_token_single(std::uint32_t id)
	{
		return gsc::gsc_ctx->token_name(id);
	}

	unsigned int find_token_id(const std::string& name)
	{
		const auto id = gsc::gsc_ctx->token_id(name);
		if (id)
		{
			return id;
		}

		const auto parsed_id = parse_token_id(name);
		if (parsed_id)
		{
			return parsed_id;
		}

		return game::SL_GetCanonicalString(name.data());
	}

	script_function get_function_by_index(const std::uint32_t index)
	{
		static const auto function_table = reinterpret_cast<script_function*>(game::select(0xAC9BE40, 0xBFA90C0, 0x9D1E000));
		static const auto method_table = reinterpret_cast<script_function*>(game::select(0xAC9DC70, 0xBFAAEF0, 0x9D1FE30));

		if (index >= 1 && index <= 0x3C6)
		{
			return function_table[index - 1];
		}

		if (index >= 0x8000)
		{
			return method_table[index - 0x8000];
		}

		return nullptr;
	}

	script_function find_function(const std::string& name, const bool prefer_global)
	{
		const auto index = find_function_index(name, prefer_global);
		if (index < 0) return nullptr;

		return get_function_by_index(index);
	}
}
