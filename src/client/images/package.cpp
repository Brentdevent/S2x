#include <std_include.hpp>

#include "package.hpp"
#include <utils/dds.hpp>

#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace images::package
{
	namespace
	{
		void require(const bool value)
		{
			if (!value)
			{
				throw std::runtime_error("invalid or truncated image PAK chunk");
			}
		}

		// Bounded decoder for the public LZ4 block format (no external dictionary).
		void decode_block(const std::string_view input, char* output, const std::size_t size)
		{
			std::size_t in{};
			std::size_t out{};
			const auto byte = [&]() -> unsigned
			{
				require(in < input.size());

				return static_cast<unsigned char>(input[in++]);
			};

			const auto length = [&](std::size_t initial)
			{
				if (initial == 15)
				{
					unsigned extension;
					do
					{
						extension = byte();
						require(initial <= size && extension <= size - initial);
						initial += extension;
					} while (extension == 255);
				}

				return initial;
			};

			while (in < input.size())
			{
				const auto token = byte();
				const auto literals = length(token >> 4);
				require(literals <= input.size() - in && literals <= size - out);
				std::memcpy(output + out, input.data() + in, literals);
				in += literals;
				out += literals;
				if (in == input.size())
				{
					break;
				}

				const auto low = byte();
				const auto distance = low | (byte() << 8);
				require(distance && distance <= out);
				const auto count = length(token & 15) + 4;
				require(count <= size - out);

				// Matches may overlap; memcpy/memmove cannot implement this expansion.
				for (std::size_t i = 0; i < count; ++i, ++out)
				{
					output[out] = output[out - distance];
				}
			}

			require(out == size);
		}
	}

	std::string decode(const std::string_view bytes, const std::size_t expected_size)
	{
		require(expected_size && expected_size <= utils::dds::max_file_size && bytes.size() >= 12
			&& bytes.size() <= utils::dds::max_file_size);
		std::uint64_t total;
		std::memcpy(&total, bytes.data(), sizeof(total));
		require(total == expected_size);
		std::string output(expected_size, '\0');
		std::size_t in = 12, out = 0;
		while (out < output.size())
		{
			require(in <= bytes.size() && bytes.size() - in >= 8);
			std::uint32_t compressed;
			std::uint32_t uncompressed;
			std::memcpy(&compressed, bytes.data() + in, 4);
			std::memcpy(&uncompressed, bytes.data() + in + 4, 4);
			in += 8;
			require(
				compressed && compressed <= bytes.size() - in && uncompressed && uncompressed <= output.size() - out);
			decode_block(bytes.substr(in, compressed), output.data() + out, uncompressed);
			in = (in + compressed + 3) & ~std::size_t(3);
			require(in <= bytes.size());
			out += uncompressed;
		}

		require(in == bytes.size());

		return output;
	}
}
