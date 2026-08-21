#include <std_include.hpp>

#include "file_updater.hpp"
#include "updater.hpp"
#include "update_source.hpp"

#include <utils/concurrency.hpp>
#include <utils/cryptography.hpp>
#include <utils/finally.hpp>
#include <utils/flags.hpp>
#include <utils/http.hpp>
#include <utils/io.hpp>
#include <utils/nt.hpp>
#include <utils/string.hpp>

#define UPDATE_HOST_BINARY "s2x.exe"

namespace updater
{
	namespace
	{
		std::string get_cache_buster()
		{
			return "?" + std::to_string(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count());
		}

		bool is_lowercase_sha1(const std::string& hash)
		{
			if (hash.size() != 40)
			{
				return false;
			}

			for (const auto character : hash)
			{
				if ((character < '0' || character > '9') && (character < 'a' || character > 'f'))
				{
					return false;
				}
			}

			return true;
		}

		bool is_safe_manifest_name(const std::string& name)
		{
			if (name.empty() || name.front() == '/' || name.back() == '/' || name.find('\\') != std::string::npos ||
				name.find("//") != std::string::npos || name.find('\0') != std::string::npos)
			{
				return false;
			}

			const std::filesystem::path path{name};
			if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
			{
				return false;
			}

			for (const auto& part : path)
			{
				const auto value = part.generic_string();
				if (value.empty() || value == "." || value == ".." || value.back() == '.' || value.back() == ' ')
				{
					return false;
				}

				if (value.find_first_of("<>:\"|?*") != std::string::npos)
				{
					return false;
				}

				for (const auto raw_character : value)
				{
					const auto character = static_cast<unsigned char>(raw_character);
					if (character < 0x20)
					{
						return false;
					}
				}

				auto device_name = utils::string::to_lower(value.substr(0, value.find('.')));
				if (device_name == "con" || device_name == "prn" || device_name == "aux" || device_name == "nul" ||
					(device_name.size() == 4 && (device_name.starts_with("com") || device_name.starts_with("lpt")) &&
						device_name[3] >= '1' && device_name[3] <= '9'))
				{
					return false;
				}
			}

			const auto normalized = path.lexically_normal().generic_string();
			return normalized == UPDATE_HOST_BINARY ||
				(normalized == name && normalized.starts_with("data/") && normalized.size() > 5);
		}

		std::vector<file_info> parse_file_infos(const std::string& json)
		{
			rapidjson::Document document{};
			document.Parse(json.data(), json.size());

			if (document.HasParseError() || !document.IsArray())
			{
				throw std::runtime_error("The update manifest is not a valid JSON array.");
			}

			std::vector<file_info> files{};
			std::unordered_set<std::string> names{};

			for (const auto& element : document.GetArray())
			{
				if (!element.IsArray() || element.Size() != 3 || !element[0].IsString() ||
					!element[1].IsUint64() || !element[2].IsString())
				{
					throw std::runtime_error("The update manifest contains a malformed file entry.");
				}

				file_info info{};
				info.name.assign(element[0].GetString(), element[0].GetStringLength());
				const auto size = element[1].GetUint64();
				if (size > std::numeric_limits<std::size_t>::max())
				{
					throw std::runtime_error("The update manifest contains an unsupported file size.");
				}

				info.size = static_cast<std::size_t>(size);
				info.hash.assign(element[2].GetString(), element[2].GetStringLength());

				if (!is_safe_manifest_name(info.name))
				{
					throw std::runtime_error("The update manifest contains an unsafe path: " + info.name);
				}

				if (!is_lowercase_sha1(info.hash))
				{
					throw std::runtime_error("The update manifest contains an invalid SHA-1 hash.");
				}

				if (!names.emplace(utils::string::to_lower(info.name)).second)
				{
					throw std::runtime_error("The update manifest contains duplicate file paths.");
				}

				files.emplace_back(std::move(info));
			}

			return files;
		}

		std::vector<file_info> get_file_infos()
		{
			const auto json = utils::http::get_data(std::string{update_source::manifest} + get_cache_buster());
			if (!json)
			{
				throw std::runtime_error("Unable to download the update manifest.");
			}

			return parse_file_infos(*json);
		}

		const file_info* find_host_file_info(const std::vector<file_info>& outdated_files)
		{
			for (const auto& file : outdated_files)
			{
				if (file.name == UPDATE_HOST_BINARY)
				{
					return &file;
				}
			}

			return nullptr;
		}

		std::size_t get_optimal_concurrent_download_count(const std::size_t file_count)
		{
			auto cores = static_cast<std::size_t>(std::thread::hardware_concurrency());
			cores = (cores * 2) / 3;
			return std::max<std::size_t>(1, std::min(cores, file_count));
		}

		std::string encode_url_path(const std::string& path)
		{
			constexpr char hex[] = "0123456789ABCDEF";
			std::string result{};
			result.reserve(path.size());

			for (const auto raw_character : path)
			{
				const auto character = static_cast<unsigned char>(raw_character);
				if (std::isalnum(character) || character == '-' || character == '_' || character == '.' ||
					character == '~' || character == '/')
				{
					result.push_back(static_cast<char>(character));
				}
				else
				{
					result.push_back('%');
					result.push_back(hex[character >> 4]);
					result.push_back(hex[character & 0xF]);
				}
			}

			return result;
		}

		std::filesystem::path get_temporary_filename(const std::filesystem::path& target)
		{
			static std::atomic<std::uint64_t> counter{0};

			for (;;)
			{
				auto temporary = target;
				temporary += ".s2x-update-" + std::to_string(GetCurrentProcessId()) + "-" +
					std::to_string(counter++) + ".tmp";

				if (!utils::io::file_exists(temporary.wstring()))
				{
					return temporary;
				}
			}
		}
	}

	file_updater::file_updater(progress_listener& listener, std::filesystem::path base,
	                           std::filesystem::path process_file)
		: file_updater(listener, std::move(base), std::move(process_file), update_source::files,
		               file_hash::sha1, {})
	{
	}

	file_updater::file_updater(progress_listener& listener, std::filesystem::path base,
	                           std::filesystem::path process_file, std::string update_folder,
	                           const file_hash hash, std::unordered_set<std::string> allowed_root_files)
		: listener_(listener)
		, base_(std::move(base))
		, process_file_(std::move(process_file))
		, dead_process_file_(process_file_)
		, update_folder_(std::move(update_folder))
		, hash_(hash)
		, allowed_root_files_(std::move(allowed_root_files))
	{
		this->dead_process_file_ += ".old";
	}

	void file_updater::run() const
	{
		this->run(get_file_infos());
	}

	void file_updater::run(const std::vector<file_info>& files) const
	{
		for (const auto& file : files)
		{
			this->validate_file_path(file);
		}

		const auto outdated_files = this->get_outdated_files(files);
		if (outdated_files.empty())
		{
			return;
		}

		this->update_host_binary(outdated_files);
		this->update_files(outdated_files);
	}

	void file_updater::update_file(const file_info& file) const
	{
		auto query = file.hash;
		if (!file.version.empty())
		{
			query = "version=" + encode_url_path(file.version) + "&hash=" + file.hash;
		}

		const auto url = this->update_folder_ + encode_url_path(file.name) + "?" + query;
		const auto data = utils::http::get_data(url, {}, [this, &file](const std::size_t progress)
		{
			this->listener_.file_progress(file, progress);
		});

		if (!data || data->size() != file.size || this->get_hash(*data) != file.hash)
		{
			throw std::runtime_error("Failed to download or verify: " + file.name);
		}

		const auto output_file = this->get_drive_filename(file);
		const auto temporary_file = get_temporary_filename(output_file);
		const auto _ = utils::finally([&temporary_file]
		{
			utils::io::remove_file(temporary_file);
		});

		if (!utils::io::write_file(temporary_file.wstring(), *data, false) ||
			!this->file_matches(file, temporary_file))
		{
			throw std::runtime_error("Failed to write or verify: " + file.name);
		}

		if (MoveFileExW(temporary_file.wstring().data(), output_file.wstring().data(),
		                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
		{
			throw std::runtime_error("Failed to write or verify: " + file.name);
		}
	}

	std::string file_updater::get_hash(const std::string& data) const
	{
		if (this->hash_ == file_hash::sha256)
		{
			return utils::string::to_lower(utils::cryptography::sha256::compute(data, true));
		}

		return utils::string::to_lower(utils::cryptography::sha1::compute(data, true));
	}

	std::vector<file_info> file_updater::get_outdated_files(const std::vector<file_info>& files) const
	{
		std::vector<file_info> outdated_files{};

		for (const auto& info : files)
		{
			if (this->is_outdated_file(info))
			{
				outdated_files.emplace_back(info);
			}
		}

		return outdated_files;
	}

	void file_updater::update_host_binary(const std::vector<file_info>& outdated_files) const
	{
		const auto* host_file = find_host_file_info(outdated_files);
		if (!host_file)
		{
			return;
		}

		this->move_current_process_file();

		try
		{
			this->update_files({*host_file});
		}
		catch (...)
		{
			const auto update_error = std::current_exception();
			if (!this->restore_current_process_file())
			{
				throw std::runtime_error("Executable update failed and s2x.exe could not be restored.");
			}

			std::rethrow_exception(update_error);
		}

		if (!utils::flags::has_flag("-norelaunch") && !utils::nt::relaunch_self())
		{
			throw std::runtime_error("The update was installed, but S2x could not be relaunched.");
		}

		throw update_cancelled();
	}

	void file_updater::update_files(const std::vector<file_info>& outdated_files) const
	{
		this->listener_.update_files(outdated_files);
		if (!this->listener_.supports_concurrent_updates())
		{
			for (const auto& file : outdated_files)
			{
				this->listener_.begin_file(file);
				this->update_file(file);
				this->listener_.end_file(file);
			}

			this->listener_.done_update();
			return;
		}

		const auto thread_count = get_optimal_concurrent_download_count(outdated_files.size());
		std::vector<std::thread> threads{};
		std::atomic<std::size_t> current_index{0};
		utils::concurrency::container<std::exception_ptr> exception{};

		{
			const auto join_threads = utils::finally([&threads]
			{
				for (auto& thread : threads)
				{
					if (thread.joinable())
					{
						thread.join();
					}
				}
			});

			for (std::size_t i = 0; i < thread_count; ++i)
			{
				threads.emplace_back([&]()
				{
					while (!exception.access<bool>([](const std::exception_ptr& pointer)
					{
						return static_cast<bool>(pointer);
					}))
					{
						const auto index = current_index++;
						if (index >= outdated_files.size())
						{
							break;
						}

						try
						{
							const auto& file = outdated_files[index];
							this->listener_.begin_file(file);
							this->update_file(file);
							this->listener_.end_file(file);
						}
						catch (...)
						{
							exception.access([](std::exception_ptr& pointer)
							{
								if (!pointer)
								{
									pointer = std::current_exception();
								}
							});
							return;
						}
					}
				});
			}
		}

		exception.access([](const std::exception_ptr& pointer)
		{
			if (pointer)
			{
				std::rethrow_exception(pointer);
			}
		});

		this->listener_.done_update();
	}

	bool file_updater::is_outdated_file(const file_info& file) const
	{
#if !defined(NDEBUG) || !defined(CI)
		if (file.name == UPDATE_HOST_BINARY && !utils::flags::has_flag("-update"))
		{
			return false;
		}
#endif

		return !this->file_matches(file, this->get_drive_filename(file));
	}

	bool file_updater::file_matches(const file_info& file, const std::filesystem::path& path) const
	{
		if (!utils::io::file_exists(path.wstring()) || utils::io::file_size(path.wstring()) != file.size)
		{
			return false;
		}

		std::string data{};
		return utils::io::read_file(path.wstring(), &data) && data.size() == file.size &&
			this->get_hash(data) == file.hash;
	}

	std::filesystem::path file_updater::get_drive_filename(const file_info& file) const
	{
		if (file.name == UPDATE_HOST_BINARY)
		{
			return this->process_file_;
		}

		return this->base_ / std::filesystem::path{file.name};
	}

	void file_updater::validate_file_path(const file_info& file) const
	{
		if (!this->allowed_root_files_.empty())
		{
			if (!this->allowed_root_files_.contains(utils::string::to_lower(file.name)))
			{
				throw std::runtime_error("The update manifest contains an unexpected file: " + file.name);
			}
		}
		else if (file.name == UPDATE_HOST_BINARY)
		{
			return;
		}

		auto current = this->base_;
		const auto base_attributes = GetFileAttributesW(current.wstring().data());
		if (base_attributes != INVALID_FILE_ATTRIBUTES && (base_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		{
			throw std::runtime_error("The S2x data path is a reparse point.");
		}

		for (const auto& part : std::filesystem::path{file.name})
		{
			current /= part;
			const auto attributes = GetFileAttributesW(current.wstring().data());
			if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
			{
				throw std::runtime_error("The update path contains a reparse point: " + file.name);
			}
		}
	}

	void file_updater::move_current_process_file() const
	{
		if (!utils::io::remove_file(this->dead_process_file_) ||
			!utils::io::move_file(this->process_file_, this->dead_process_file_))
		{
			throw std::runtime_error("Unable to rename s2x.exe for self-update.");
		}
	}

	bool file_updater::restore_current_process_file() const
	{
		return utils::io::remove_file(this->process_file_) &&
			utils::io::move_file(this->dead_process_file_, this->process_file_);
	}

}
