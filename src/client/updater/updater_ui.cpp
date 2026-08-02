#include <std_include.hpp>

#include "updater_ui.hpp"
#include "update_cancelled.hpp"

#include "component/console/console.hpp"

#include <utils/string.hpp>

namespace updater
{
	updater_ui::updater_ui(const bool dedicated)
		: dedicated_(dedicated)
		, progress_ui_(dedicated)
	{
	}

	updater_ui::~updater_ui() = default;

	void updater_ui::update_files(const std::vector<file_info>& files)
	{
		this->handle_cancellation();

		std::lock_guard<std::recursive_mutex> _{this->mutex_};
		this->total_files_ = files;
		this->downloaded_files_.clear();
		this->downloading_files_.clear();

		if (this->dedicated_)
		{
			console::info("[Updater] Updating %zu file(s)...\n", files.size());
			return;
		}

		this->progress_ui_.set_title("S2x Updater");
		this->progress_ui_.show(false);
		this->update_file_name();
	}

	void updater_ui::done_update()
	{
		std::lock_guard<std::recursive_mutex> _{this->mutex_};

		if (this->dedicated_)
		{
			console::info("[Updater] Update complete.\n");
		}
		else
		{
			const auto total_size = this->get_total_size();
			this->progress_ui_.set_progress(total_size, total_size);
			this->progress_ui_.set_line(1, "Update successful.");
			this->progress_ui_.set_line(2, this->get_relevant_file_name());
		}

		this->total_files_.clear();
		this->downloaded_files_.clear();
		this->downloading_files_.clear();
	}

	void updater_ui::begin_file(const file_info& file)
	{
		this->handle_cancellation();

		std::lock_guard<std::recursive_mutex> _{this->mutex_};
		if (this->dedicated_)
		{
			console::info("[Updater] Downloading %s\n", file.name.data());
		}

		this->file_progress(file, 0);
		this->update_file_name();
	}

	void updater_ui::end_file(const file_info& file)
	{
		std::lock_guard<std::recursive_mutex> _{this->mutex_};
		this->downloaded_files_.emplace_back(file);

		const auto entry = this->downloading_files_.find(file.name);
		if (entry != this->downloading_files_.end())
		{
			this->downloading_files_.erase(entry);
		}

		this->update_progress();
		this->update_file_name();
	}

	void updater_ui::file_progress(const file_info& file, const std::size_t progress)
	{
		this->handle_cancellation();

		std::lock_guard<std::recursive_mutex> _{this->mutex_};
		this->downloading_files_[file.name] = {progress, file.size};
		this->update_progress();
	}

	bool updater_ui::supports_concurrent_updates() const
	{
		return this->dedicated_;
	}

	void updater_ui::handle_cancellation() const
	{
		if (this->progress_ui_.is_cancelled())
		{
			throw update_cancelled();
		}
	}

	void updater_ui::update_progress() const
	{
		if (this->dedicated_)
		{
			return;
		}

		std::lock_guard<std::recursive_mutex> _{this->mutex_};
		this->progress_ui_.set_progress(this->get_downloaded_size(), this->get_total_size());
	}

	void updater_ui::update_file_name() const
	{
		if (this->dedicated_)
		{
			return;
		}

		std::lock_guard<std::recursive_mutex> _{this->mutex_};
		const auto downloaded_file_count = this->get_downloaded_files();
		const auto total_file_count = this->get_total_files();

		if (downloaded_file_count == total_file_count)
		{
			this->progress_ui_.set_line(1, "Update successful.");
		}
		else
		{
			this->progress_ui_.set_line(1, utils::string::va("Updating files... (%zu/%zu)",
				downloaded_file_count, total_file_count));
		}

		this->progress_ui_.set_line(2, this->get_relevant_file_name());
	}

	std::size_t updater_ui::get_total_size() const
	{
		std::lock_guard<std::recursive_mutex> _{this->mutex_};
		std::size_t total_size = 0;

		for (const auto& file : this->total_files_)
		{
			total_size += file.size;
		}

		return total_size;
	}

	std::size_t updater_ui::get_downloaded_size() const
	{
		std::lock_guard<std::recursive_mutex> _{this->mutex_};
		std::size_t downloaded_size = 0;

		for (const auto& file : this->downloaded_files_)
		{
			downloaded_size += file.size;
		}

		for (const auto& file : this->downloading_files_)
		{
			downloaded_size += file.second.first;
		}

		return downloaded_size;
	}

	std::size_t updater_ui::get_total_files() const
	{
		std::lock_guard<std::recursive_mutex> _{this->mutex_};
		return this->total_files_.size();
	}

	std::size_t updater_ui::get_downloaded_files() const
	{
		std::lock_guard<std::recursive_mutex> _{this->mutex_};
		return this->downloaded_files_.size();
	}

	std::string updater_ui::get_relevant_file_name() const
	{
		std::lock_guard<std::recursive_mutex> _{this->mutex_};
		std::string name{};
		auto smallest = std::numeric_limits<std::size_t>::max();

		for (const auto& file : this->downloading_files_)
		{
			if (file.second.second < smallest)
			{
				smallest = file.second.second;
				name = file.first;
			}
		}

		if (name.empty() && !this->downloaded_files_.empty())
		{
			name = this->downloaded_files_.back().name;
		}

		return name;
	}
}
