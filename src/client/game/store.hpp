#pragma once

namespace game::store
{
	struct signature
	{
		std::uint32_t steam_rva;
		const char* pattern;
		std::int32_t offset;
		std::uint8_t operand;
		std::uint8_t length;
		std::uint8_t section;
	};

	extern const std::uint32_t signature_image_timestamp;
	extern const std::vector<signature> signatures;

	constexpr std::uint32_t supported_timestamp = 0x67467ACE;
	constexpr std::uint32_t supported_image_size = 0x12967000;

	bool is_supported_binary(std::uint32_t timestamp, std::uint32_t image_size);
	bool is_supported_binary_file(const std::filesystem::path& path);

	struct initialize_result
	{
		size_t resolved{};
		size_t failed{};
		bool from_cache{};
		std::filesystem::path cache_path{};
	};

	initialize_result initialize();
	bool has(size_t steam_rva);
}
