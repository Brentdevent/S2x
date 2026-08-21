#include <std_include.hpp>

#include "updater.hpp"
#include "file_updater.hpp"
#include "update_source.hpp"
#include "updater_ui.hpp"

#include <game/game.hpp>

#include <utils/http.hpp>
#include <utils/nt.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>

namespace updater
{
	namespace
	{
		constexpr auto store_manifest_version = 1u;
		constexpr auto store_mp_runtime = "s2x_mp64_ship.exe";
		constexpr auto store_sp_runtime = "s2x_sp64_ship.exe";

		bool is_lowercase_sha256(const std::string& hash)
		{
			if (hash.size() != 64)
			{
				return false;
			}

			return std::ranges::all_of(hash, [](const char character)
			{
				return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
			});
		}

		bool is_safe_runtime_version(const std::string& version)
		{
			if (version.empty() || version.size() > 64)
			{
				return false;
			}

			return std::ranges::all_of(version, [](const unsigned char character)
			{
				return std::isalnum(character) || character == '.' || character == '-' || character == '_';
			});
		}

		bool is_store_runtime_name(const std::string& name)
		{
			return name == store_mp_runtime || name == store_sp_runtime;
		}

		file_info parse_store_runtime_manifest(const std::string& json, const std::string& required_file)
		{
			rapidjson::Document document{};
			document.Parse(json.data(), json.size());

			if (document.HasParseError() || !document.IsObject() ||
				!document.HasMember("manifest_version") || !document["manifest_version"].IsUint() ||
				document["manifest_version"].GetUint() != store_manifest_version ||
				!document.HasMember("files") || !document["files"].IsArray())
			{
				throw std::runtime_error("The Store runtime manifest is invalid or unsupported.");
			}

			std::optional<file_info> required_info{};
			std::unordered_set<std::string> names{};

			for (const auto& element : document["files"].GetArray())
			{
				if (!element.IsObject() || !element.HasMember("filename") || !element["filename"].IsString() ||
					!element.HasMember("version") || !element["version"].IsString() ||
					!element.HasMember("size") || !element["size"].IsUint64() ||
					!element.HasMember("sha256") || !element["sha256"].IsString())
				{
					throw std::runtime_error("The Store runtime manifest contains a malformed file entry.");
				}

				file_info info{};
				info.name.assign(element["filename"].GetString(), element["filename"].GetStringLength());
				info.version.assign(element["version"].GetString(), element["version"].GetStringLength());
				info.hash.assign(element["sha256"].GetString(), element["sha256"].GetStringLength());

				const auto size = element["size"].GetUint64();
				if (!is_store_runtime_name(info.name) || !is_safe_runtime_version(info.version) || size == 0 ||
					size > std::numeric_limits<std::size_t>::max() || !is_lowercase_sha256(info.hash))
				{
					throw std::runtime_error("The Store runtime manifest contains an invalid file entry.");
				}

				info.size = static_cast<std::size_t>(size);
				if (!names.emplace(info.name).second)
				{
					throw std::runtime_error("The Store runtime manifest contains duplicate file entries.");
				}

				if (info.name == required_file)
				{
					required_info = std::move(info);
				}
			}

			if (!required_info)
			{
				throw std::runtime_error("The Store runtime manifest does not contain " + required_file + ".");
			}

			return std::move(*required_info);
		}
	}

	void cleanup()
	{
		const auto self = utils::nt::library::get_by_address(cleanup);
		auto old_self_file = self.get_path();
		old_self_file += ".old";

		for (auto attempt = 0; attempt < 4; ++attempt)
		{
			if (utils::io::remove_file(old_self_file) && !utils::io::file_exists(old_self_file.wstring()))
			{
				return;
			}

			if (attempt < 3)
			{
				std::this_thread::sleep_for(2s);
			}
		}

		throw std::runtime_error("Unable to remove s2x.exe.old.");
	}

	void run(const std::filesystem::path& base)
	{
		const auto self = utils::nt::library::get_by_address(run);
		const auto self_file = self.get_path();

		updater_ui updater_ui{game::environment::is_dedicated()};
		const file_updater file_updater{updater_ui, base, self_file};
		file_updater.run();
	}

	void run_store_runtime_update(const std::filesystem::path& base, const std::string& required_file)
	{
		if (!is_store_runtime_name(required_file))
		{
			throw std::runtime_error("Unsupported Store runtime file: " + required_file);
		}

		const auto cache_buster = "?" + std::to_string(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		const auto json = utils::http::get_data(std::string{update_source::store_runtime_manifest} + cache_buster);
		if (!json)
		{
			throw std::runtime_error("Unable to download the Store runtime manifest.");
		}

		const auto info = parse_store_runtime_manifest(*json, required_file);
		const auto self = utils::nt::library::get_by_address(run_store_runtime_update);
		updater_ui updater_ui{game::environment::is_dedicated()};
		const file_updater file_updater{
			updater_ui, base, self.get_path(), update_source::store_runtime_files, file_hash::sha256,
			{store_mp_runtime, store_sp_runtime}
		};
		file_updater.run({info});
	}
}
