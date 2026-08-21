#pragma once

#include "progress_listener.hpp"

namespace updater
{
	enum class file_hash
	{
		sha1,
		sha256,
	};

	class file_updater
	{
	public:
		file_updater(progress_listener& listener, std::filesystem::path base, std::filesystem::path process_file);
		file_updater(progress_listener& listener, std::filesystem::path base, std::filesystem::path process_file,
		             std::string update_folder, file_hash hash,
		             std::unordered_set<std::string> allowed_root_files);

		void run() const;
		void run(const std::vector<file_info>& files) const;

		[[nodiscard]] std::vector<file_info> get_outdated_files(const std::vector<file_info>& files) const;

		void update_host_binary(const std::vector<file_info>& outdated_files) const;
		void update_files(const std::vector<file_info>& outdated_files) const;

	private:
		progress_listener& listener_;

		std::filesystem::path base_;
		std::filesystem::path process_file_;
		std::filesystem::path dead_process_file_;
		std::string update_folder_;
		file_hash hash_;
		std::unordered_set<std::string> allowed_root_files_;

		void update_file(const file_info& file) const;
		[[nodiscard]] std::string get_hash(const std::string& data) const;

		[[nodiscard]] bool is_outdated_file(const file_info& file) const;
		[[nodiscard]] bool file_matches(const file_info& file, const std::filesystem::path& path) const;
		[[nodiscard]] std::filesystem::path get_drive_filename(const file_info& file) const;
		void validate_file_path(const file_info& file) const;

		void move_current_process_file() const;
		[[nodiscard]] bool restore_current_process_file() const;
	};
}
