#pragma once

#include "byte_buffer.hpp"

namespace demonware::content_store
{
	constexpr unsigned short emblem_category = 8;
	constexpr unsigned short first_emblem_slot = 1000;
	constexpr unsigned short emblem_slots = 30;
	constexpr unsigned int emblem_size = 1856;

	constexpr unsigned short paintjob_category = 10;
	constexpr unsigned short first_paintjob_slot = 3000;
	constexpr unsigned short paintjob_slots = 30;
	constexpr unsigned int paintjob_size = 13632;

	constexpr unsigned short storage_server_id = 1;
	constexpr auto hostname = "s2x-content.demonware.net";

	using file_tags = std::vector<std::pair<std::uint64_t, std::uint64_t>>;

	struct file_record
	{
		std::uint64_t id{};
		std::uint64_t owner{};
		unsigned short slot{};
		unsigned short category{};

		unsigned int created{};
		unsigned int modified{};

		std::string name{};
		std::string metadata{};
		file_tags tags{};
		std::string data{};
	};

	bool valid_slot(unsigned short slot);
	bool valid_file(unsigned short category, unsigned short slot, size_t size);

	bool read_tags(byte_buffer& buffer, file_tags& tags);
	void write_tags(byte_buffer& buffer, const file_tags& tags);

	std::string download_url(std::uint64_t id);
	std::vector<file_record> list_files(std::uint64_t owner, unsigned short category);
	std::optional<file_record> find_file(std::uint64_t id);

	std::string begin_upload(file_record record);
	bool stage_upload(const std::string& token, const std::string& data);
	std::optional<std::uint64_t> finish_upload(const std::string& token, const file_record& record);

	void delete_file(unsigned short slot);
	void reset_uploads();
}
