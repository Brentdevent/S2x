#pragma once

#include "progress_listener.hpp"

#include <utils/progress_ui.hpp>

namespace updater
{
	class updater_ui final : public progress_listener
	{
	public:
		explicit updater_ui(bool dedicated);
		~updater_ui() override;

	private:
		bool dedicated_;
		mutable std::recursive_mutex mutex_{};
		std::vector<file_info> total_files_{};
		std::vector<file_info> downloaded_files_{};
		std::map<std::string, std::pair<std::size_t, std::size_t>> downloading_files_{};
		utils::progress_ui progress_ui_;

		void update_files(const std::vector<file_info>& files) override;
		void done_update() override;

		void begin_file(const file_info& file) override;
		void end_file(const file_info& file) override;

		void file_progress(const file_info& file, std::size_t progress) override;
		[[nodiscard]] bool supports_concurrent_updates() const override;

		void handle_cancellation() const;
		void update_progress() const;
		void update_file_name() const;

		[[nodiscard]] std::size_t get_total_size() const;
		[[nodiscard]] std::size_t get_downloaded_size() const;
		[[nodiscard]] std::size_t get_total_files() const;
		[[nodiscard]] std::size_t get_downloaded_files() const;
		[[nodiscard]] std::string get_relevant_file_name() const;
	};
}
