#include <std_include.hpp>
#include "content_store.hpp"
#include "runtime_context.hpp"
#include "game/player_profile.hpp"

#include <utils/cryptography.hpp>

namespace demonware::content_store
{
	namespace
	{
		struct pending_upload
		{
			file_record record{};
			std::chrono::steady_clock::time_point expires{};
		};

		std::mutex store_mutex;
		std::unordered_map<std::string, pending_upload> uploads;

		std::filesystem::path file_path(const std::uint64_t owner, const unsigned short slot)
		{
			return player_profile::user_directory() / "paintjobs" / std::to_string(owner)
				/ (std::to_string(slot) + ".ugc");
		}

		std::uint64_t random_id()
		{
			std::uint64_t value{};
			do
			{
				utils::cryptography::random::get_data(&value, sizeof(value));
				value &= INT64_MAX;
			} while (!value);
			return value;
		}

		void expire_uploads()
		{
			const auto now = std::chrono::steady_clock::now();
			const auto owner = runtime_context::get_local_user_id();
			std::erase_if(uploads, [&](const auto& entry)
			{
				return entry.second.expires <= now || entry.second.record.owner != owner;
			});
		}

		std::optional<file_record> load_file(const std::uint64_t owner, const unsigned short slot)
		{
			const auto path = file_path(owner, slot);
			if (!std::filesystem::exists(path))
			{
				return {};
			}

			std::ifstream stream(path, std::ios::binary | std::ios::ate);
			const auto size = stream.tellg();
			if (size < 0 || size > 32768)
			{
				throw std::runtime_error("Unable to read paint job file");
			}
			std::string data(static_cast<size_t>(size), '\0');
			stream.seekg(0);
			if (!stream.read(data.data(), size))
			{
				throw std::runtime_error("Unable to read paint job file");
			}

			byte_buffer buffer(std::move(data));
			unsigned int version{};
			file_record record;
			if (!buffer.read_uint32(&version) || version != 1
				|| !buffer.read_uint64(&record.id) || !record.id
				|| !buffer.read_uint64(&record.owner) || record.owner != owner
				|| !buffer.read_uint16(&record.slot) || record.slot != slot
				|| !buffer.read_uint16(&record.category) || record.category != paintjob_category
				|| !buffer.read_uint32(&record.created) || !buffer.read_uint32(&record.modified)
				|| !buffer.read_string(&record.name, 127) || !buffer.read_blob(&record.metadata, 512)
				|| !read_tags(buffer, record.tags) || !buffer.read_blob(&record.data, paintjob_size)
				|| record.data.size() != paintjob_size || buffer.remaining_size())
			{
				throw std::runtime_error("Invalid paint job file");
			}
			return record;
		}

		void save_file(const file_record& record)
		{
			byte_buffer buffer;
			buffer.write_uint32(1);
			buffer.write_uint64(record.id);
			buffer.write_uint64(record.owner);
			buffer.write_uint16(record.slot);
			buffer.write_uint16(record.category);
			buffer.write_uint32(record.created);
			buffer.write_uint32(record.modified);
			buffer.write_string(record.name);
			buffer.write_blob(record.metadata);
			write_tags(buffer, record.tags);
			buffer.write_blob(record.data);

			const auto path = file_path(record.owner, record.slot);
			auto temporary = path;
			temporary += ".tmp";
			std::filesystem::create_directories(path.parent_path());
			const auto handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
			{
				throw std::runtime_error("Unable to create paint job file");
			}

			const auto& data = buffer.get_buffer();
			DWORD written{};
			const auto saved = WriteFile(handle, data.data(), static_cast<DWORD>(data.size()), &written, nullptr)
				&& written == data.size() && FlushFileBuffers(handle);
			const auto closed = CloseHandle(handle);
			if (!saved || !closed || !MoveFileExW(temporary.c_str(), path.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			{
				DeleteFileW(temporary.c_str());
				throw std::runtime_error("Unable to save paint job file");
			}
		}
	}

	bool valid_slot(const unsigned short slot)
	{
		return slot >= first_paintjob_slot && slot < first_paintjob_slot + paintjob_slots;
	}

	bool read_tags(byte_buffer& buffer, file_tags& tags)
	{
		unsigned int count{}, size{};
		if (!buffer.read_array_header(10, &count, &size) || count > 120 || count % 2 || (count && size != 8))
		{
			return false;
		}
		tags.clear();
		buffer.set_use_data_types(false);
		bool valid = true;
		for (unsigned int i = 0; i < count / 2 && valid; ++i)
		{
			std::uint64_t key{}, value{};
			valid = buffer.read_uint64(&key) && buffer.read_uint64(&value);
			tags.emplace_back(key, value);
		}
		buffer.set_use_data_types(true);
		return valid;
	}

	void write_tags(byte_buffer& buffer, const file_tags& tags)
	{
		buffer.write_array_header(10, static_cast<unsigned int>(tags.size() * 2), 8);
		buffer.set_use_data_types(false);
		for (const auto& [key, value] : tags)
		{
			buffer.write_uint64(key);
			buffer.write_uint64(value);
		}
		buffer.set_use_data_types(true);
	}

	std::string download_url(const std::uint64_t id)
	{
		return "http://" + std::string(hostname) + "/files/" + std::to_string(id);
	}

	std::vector<file_record> list_files(const std::uint64_t owner)
	{
		std::lock_guard lock(store_mutex);
		std::vector<file_record> result;
		if (!owner || owner != runtime_context::get_local_user_id())
		{
			return result;
		}
		for (unsigned short slot = first_paintjob_slot; slot < first_paintjob_slot + paintjob_slots; ++slot)
		{
			if (auto record = load_file(owner, slot))
			{
				result.push_back(std::move(*record));
			}
		}
		return result;
	}

	std::optional<file_record> find_file(const std::uint64_t id)
	{
		for (auto& record : list_files(runtime_context::get_local_user_id()))
		{
			if (record.id == id)
			{
				return std::move(record);
			}
		}
		return {};
	}

	std::string begin_upload(file_record record)
	{
		std::lock_guard lock(store_mutex);
		expire_uploads();
		if (!record.owner || record.owner != runtime_context::get_local_user_id()
			|| !valid_slot(record.slot) || record.category != paintjob_category
			|| record.name.size() > 127 || record.metadata.size() > 512)
		{
			return {};
		}

		const auto existing = load_file(record.owner, record.slot);
		record.id = existing ? existing->id : random_id();
		record.created = existing ? existing->created : static_cast<unsigned int>(std::time(nullptr));
		std::erase_if(uploads, [&](const auto& entry) { return entry.second.record.slot == record.slot; });
		std::string token;
		do
		{
			token = std::to_string(random_id());
		} while (uploads.contains(token));
		uploads.emplace(token, pending_upload{std::move(record), std::chrono::steady_clock::now() + 5min});
		return token;
	}

	bool stage_upload(const std::string& token, const std::string& data)
	{
		std::lock_guard lock(store_mutex);
		expire_uploads();
		const auto found = uploads.find(token);
		if (found == uploads.end() || data.size() != paintjob_size)
		{
			return false;
		}
		found->second.record.data = data;
		return true;
	}

	std::optional<std::uint64_t> finish_upload(const std::string& token, const file_record& record)
	{
		std::lock_guard lock(store_mutex);
		expire_uploads();
		const auto found = uploads.find(token);
		if (found == uploads.end())
		{
			return {};
		}
		auto& pending = found->second.record;
		if (pending.owner != record.owner || pending.slot != record.slot || pending.category != record.category
			|| pending.name != record.name || pending.metadata != record.metadata
			|| pending.data.size() != paintjob_size || record.tags.size() > 60)
		{
			return {};
		}
		pending.tags = record.tags;
		pending.modified = static_cast<unsigned int>(std::time(nullptr));
		save_file(pending);
		const auto id = pending.id;
		uploads.erase(found);
		return id;
	}

	void delete_file(const unsigned short slot)
	{
		std::lock_guard lock(store_mutex);
		const auto owner = runtime_context::get_local_user_id();
		if (!owner || !valid_slot(slot))
		{
			throw std::runtime_error("Invalid paint job slot");
		}
		std::filesystem::remove(file_path(owner, slot));
		std::erase_if(uploads, [&](const auto& entry) { return entry.second.record.slot == slot; });
	}

	void reset_uploads()
	{
		std::lock_guard lock(store_mutex);
		uploads.clear();
	}
}
