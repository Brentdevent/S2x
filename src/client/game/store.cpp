#include <std_include.hpp>

#include "game.hpp"
#include "store.hpp"

#include <utils/io.hpp>
#include <utils/nt.hpp>
#include <utils/string.hpp>

namespace game::store
{
	namespace
	{
		constexpr std::uint8_t section_text = 0;
		constexpr std::uint8_t section_rdata = 1;

		constexpr size_t cg_global_rva = 0x9E11A70;
		constexpr size_t cgs_global_rva = 0x9E11A78;
		constexpr size_t client_active_global_rva = 0x115ADA0;
		constexpr size_t cg_stride = 0x43A980;
		constexpr size_t cgs_stride = 0x16F38;
		constexpr size_t client_active_stride = 0x165A0;

		constexpr size_t cg_get_local_client_static_steam_rva = 0x461E0;
		constexpr size_t cl_get_local_client_active_steam_rva = 0x795D0;

		std::unordered_map<size_t, size_t> addresses{};

		struct compiled_pattern
		{
			const signature* sig{};
			std::vector<std::uint8_t> bytes{};
			std::vector<std::uint8_t> mask{};
			size_t anchor{};
		};

		struct section_range
		{
			std::uint8_t* start{};
			size_t size{};
		};

		const std::byte* read_global_array(const size_t global_rva, const size_t stride, const int index)
		{
			const auto* base = *reinterpret_cast<std::byte* const*>(get_base() + global_rva);
			return base ? base + stride * static_cast<size_t>(index) : nullptr;
		}

		const std::byte* cg_get_local_client_static(const int local_client_num)
		{
			return read_global_array(cgs_global_rva, cgs_stride, local_client_num);
		}

		const std::byte* cl_get_local_client_active(const int local_client_num)
		{
			return read_global_array(client_active_global_rva, client_active_stride, local_client_num);
		}

		std::uint8_t parse_nibble(const char value)
		{
			if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
			if (value >= 'A' && value <= 'F') return static_cast<std::uint8_t>(value - 'A' + 10);
			if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
			throw std::runtime_error("Invalid store signature pattern");
		}

		compiled_pattern compile(const signature& sig)
		{
			compiled_pattern result{};
			result.sig = &sig;

			const std::string_view pattern = sig.pattern;
			for (size_t i = 0; i < pattern.size();)
			{
				if (pattern[i] == ' ')
				{
					++i;
				}
				else if (pattern[i] == '?')
				{
					result.bytes.push_back(0);
					result.mask.push_back(0);
					++i;
				}
				else
				{
					if (i + 1 >= pattern.size())
					{
						throw std::runtime_error("Invalid store signature pattern");
					}

					result.bytes.push_back(static_cast<std::uint8_t>(parse_nibble(pattern[i]) << 4 | parse_nibble(pattern[i + 1])));
					result.mask.push_back(1);
					i += 2;
				}
			}

			return result;
		}

		std::uint64_t hash_signatures()
		{
			std::uint64_t hash = 0xCBF29CE484222325ull;
			const auto mix = [&hash](const void* data, const size_t size)
			{
				const auto* bytes = static_cast<const std::uint8_t*>(data);
				for (size_t i = 0; i < size; ++i)
				{
					hash ^= bytes[i];
					hash *= 0x100000001B3ull;
				}
			};

			for (const auto& sig : signatures)
			{
				mix(&sig.steam_rva, sizeof(sig.steam_rva));
				mix(sig.pattern, std::strlen(sig.pattern));
				mix(&sig.offset, sizeof(sig.offset));
				mix(&sig.operand, sizeof(sig.operand));
				mix(&sig.length, sizeof(sig.length));
				mix(&sig.section, sizeof(sig.section));
			}

			return hash;
		}

		section_range get_section(const utils::nt::library& host, const char* name)
		{
			for (const auto* section : host.get_section_headers())
			{
				if (std::strncmp(reinterpret_cast<const char*>(section->Name), name, sizeof(section->Name)) == 0)
				{
					return {host.get_ptr() + section->VirtualAddress, section->Misc.VirtualSize};
				}
			}

			throw std::runtime_error(utils::string::va("Microsoft Store binary has no %s section", name));
		}

		void scan_section(const section_range& range, std::vector<compiled_pattern>& patterns,
			std::unordered_map<size_t, size_t>& result)
		{
			if (patterns.empty() || range.size < 2)
			{
				return;
			}

			std::vector<std::uint32_t> frequency(0x10000);
			for (size_t i = 0; i + 1 < range.size; ++i)
			{
				++frequency[range.start[i] | range.start[i + 1] << 8];
			}

			std::vector<std::vector<size_t>> buckets(0x10000);
			for (size_t index = 0; index < patterns.size(); ++index)
			{
				auto& pattern = patterns[index];
				auto best = std::numeric_limits<std::uint32_t>::max();
				for (size_t i = 0; i + 1 < pattern.bytes.size(); ++i)
				{
					if (!pattern.mask[i] || !pattern.mask[i + 1])
					{
						continue;
					}

					const auto count = frequency[pattern.bytes[i] | pattern.bytes[i + 1] << 8];
					if (count < best)
					{
						best = count;
						pattern.anchor = i;
					}
				}

				if (best != std::numeric_limits<std::uint32_t>::max())
				{
					buckets[pattern.bytes[pattern.anchor] | pattern.bytes[pattern.anchor + 1] << 8].push_back(index);
				}
			}

			std::vector<size_t> hits(patterns.size());
			std::vector<size_t> matches(patterns.size());

			for (size_t i = 0; i + 1 < range.size; ++i)
			{
				const auto& bucket = buckets[range.start[i] | range.start[i + 1] << 8];
				for (const auto index : bucket)
				{
					const auto& pattern = patterns[index];
					if (i < pattern.anchor)
					{
						continue;
					}

					const auto start = i - pattern.anchor;
					if (start + pattern.bytes.size() > range.size)
					{
						continue;
					}

					auto matched = true;
					for (size_t j = 0; j < pattern.bytes.size(); ++j)
					{
						if (pattern.mask[j] && range.start[start + j] != pattern.bytes[j])
						{
							matched = false;
							break;
						}
					}

					if (matched)
					{
						++hits[index];
						matches[index] = start;
					}
				}
			}

			for (size_t index = 0; index < patterns.size(); ++index)
			{
				if (hits[index] != 1)
				{
					continue;
				}

				const auto& sig = *patterns[index].sig;
				const auto* instruction = range.start + matches[index] + sig.offset;
				auto target = reinterpret_cast<size_t>(instruction);

				if (sig.operand)
				{
					const auto displacement = *reinterpret_cast<const std::int32_t*>(instruction + sig.operand);
					target = reinterpret_cast<size_t>(instruction + sig.length + displacement);
				}

				result[sig.steam_rva] = target - get_base();
			}
		}

		std::unordered_map<size_t, size_t> scan(const utils::nt::library& host)
		{
			std::vector<compiled_pattern> text_patterns{};
			std::vector<compiled_pattern> rdata_patterns{};

			for (const auto& sig : signatures)
			{
				auto& target = sig.section == section_rdata ? rdata_patterns : text_patterns;
				target.push_back(compile(sig));
			}

			std::unordered_map<size_t, size_t> result{};
			scan_section(get_section(host, ".text"), text_patterns, result);
			scan_section(get_section(host, ".rdata"), rdata_patterns, result);
			return result;
		}

		std::filesystem::path get_cache_path(const std::uint32_t timestamp, const std::uint32_t image_size)
		{
			return std::filesystem::path("s2x") / "cache" / utils::string::va("store_%08X_%08X.txt", timestamp, image_size);
		}

		std::string get_cache_header(const std::uint64_t hash)
		{
			return utils::string::va("s2x-store-addresses 1 %016llX", hash);
		}

		bool load_cache(const std::filesystem::path& path, const std::uint64_t hash, const size_t image_size,
			std::unordered_map<size_t, size_t>& result)
		{
			std::string data{};
			if (!utils::io::read_file(path.string(), &data))
			{
				return false;
			}

			std::istringstream stream(data);
			std::string line{};
			if (!std::getline(stream, line))
			{
				return false;
			}

			utils::string::trim(line);
			if (line != get_cache_header(hash))
			{
				return false;
			}

			std::unordered_map<size_t, size_t> entries{};
			while (std::getline(stream, line))
			{
				size_t steam_rva{};
				size_t store_rva{};
				if (sscanf_s(line.data(), "%zX %zX", &steam_rva, &store_rva) != 2 || !store_rva || store_rva >= image_size)
				{
					return false;
				}

				entries[steam_rva] = store_rva;
			}

			result = std::move(entries);
			return true;
		}

		void save_cache(const std::filesystem::path& path, const std::uint64_t hash,
			const std::unordered_map<size_t, size_t>& entries)
		{
			std::vector<std::pair<size_t, size_t>> sorted(entries.begin(), entries.end());
			std::ranges::sort(sorted);

			std::string data = get_cache_header(hash) + "\n";
			for (const auto& [steam_rva, store_rva] : sorted)
			{
				data += utils::string::va("%zX %zX\n", steam_rva, store_rva);
			}

			utils::io::write_file(path.string(), data);
		}
	}

	bool is_supported_binary(const std::uint32_t timestamp, const std::uint32_t image_size)
	{
		return timestamp == supported_timestamp && image_size == supported_image_size;
	}

	bool is_supported_binary_file(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			return false;
		}

		IMAGE_DOS_HEADER dos_header{};
		if (!file.read(reinterpret_cast<char*>(&dos_header), sizeof(dos_header))
			|| dos_header.e_magic != IMAGE_DOS_SIGNATURE)
		{
			return false;
		}

		IMAGE_NT_HEADERS64 nt_headers{};
		if (!file.seekg(dos_header.e_lfanew)
			|| !file.read(reinterpret_cast<char*>(&nt_headers), sizeof(nt_headers))
			|| nt_headers.Signature != IMAGE_NT_SIGNATURE
			|| nt_headers.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
		{
			return false;
		}

		return is_supported_binary(nt_headers.FileHeader.TimeDateStamp, nt_headers.OptionalHeader.SizeOfImage);
	}

	initialize_result initialize()
	{
		const utils::nt::library host{};
		const auto timestamp = host.get_nt_headers()->FileHeader.TimeDateStamp;
		const auto image_size = host.get_optional_header()->SizeOfImage;

		initialize_result result{};
		result.cache_path = get_cache_path(timestamp, image_size);

		const auto hash = hash_signatures();
		std::unordered_map<size_t, size_t> entries{};

		result.from_cache = load_cache(result.cache_path, hash, image_size, entries);
		if (!result.from_cache)
		{
			entries = scan(host);
			save_cache(result.cache_path, hash, entries);
		}

		addresses.clear();
		for (const auto& [steam_rva, store_rva] : entries)
		{
			addresses[steam_rva] = get_base() + store_rva;
		}

		addresses[cg_get_local_client_static_steam_rva] = reinterpret_cast<size_t>(&cg_get_local_client_static);
		addresses[cl_get_local_client_active_steam_rva] = reinterpret_cast<size_t>(&cl_get_local_client_active);

		result.resolved = entries.size();
		result.failed = signatures.size() - entries.size();
		return result;
	}

	bool has(const size_t steam_rva)
	{
		return addresses.contains(steam_rva);
	}

	size_t resolve(const size_t steam_rva)
	{
		const auto entry = addresses.find(steam_rva);
		if (entry == addresses.end())
		{
			throw std::runtime_error(utils::string::va(
				"S2x has no Microsoft Store address for Steam address 0x%llX.", steam_rva));
		}

		return entry->second;
	}
}
