#include <std_include.hpp>
#include "collection_catalog.hpp"

#include <utils/cryptography.hpp>

namespace demonware::collection_catalog
{
	namespace
	{
		std::atomic<std::shared_ptr<const catalog>> current;
	}

	std::string rule_id(const std::uint32_t collection_id)
	{
		return rule_id("Collection_" + std::to_string(collection_id));
	}

	std::string rule_id(const std::string& name)
	{
		// S2 0x278180 hashes the symbolic rule name, then swaps the first
		// DWORD and next two WORDs before printing the UUID. No UUID version bits.
		std::array<unsigned char, 16> digest{};
		hash_state state;
		md5_init(&state);
		md5_process(&state, reinterpret_cast<const unsigned char*>(name.data()),
			static_cast<unsigned long>(name.size()));
		md5_done(&state, digest.data());
		std::reverse(digest.begin(), digest.begin() + 4);
		std::swap(digest[4], digest[5]);
		std::swap(digest[6], digest[7]);
		constexpr auto hex = "0123456789abcdef";
		std::string result;
		for (std::size_t i = 0; i < digest.size(); ++i)
		{
			result += hex[digest[i] >> 4];
			result += hex[digest[i] & 15];
			if (i == 3 || i == 5 || i == 7 || i == 9)
			{
				result += '-';
			}
		}
		return result;
	}

	std::shared_ptr<const catalog> get_snapshot()
	{
		auto result = current.load();
		// Reuse the existing DB generation's invalidation and shutdown gate.
		return result && result->source && result->source == loot_catalog::get_snapshot()
			? result : nullptr;
	}

	void publish(catalog value)
	{
		current.store(std::make_shared<const catalog>(std::move(value)));
	}
}
