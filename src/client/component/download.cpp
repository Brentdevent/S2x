#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/ui_scripting/execution.hpp"

#include "download.hpp"
#include "party_client.hpp"
#include "filesystem.hpp"
#include "network.hpp"
#include "party.hpp"
#include "scheduler.hpp"
#include "ui_scripting.hpp"

#include "console/console.hpp"

#include <utils/concurrency.hpp>
#include <utils/cryptography.hpp>
#include <utils/finally.hpp>
#include <utils/http.hpp>
#include <utils/info_string.hpp>
#include <utils/io.hpp>
#include <utils/nt.hpp>
#include <utils/string.hpp>

#include <charconv>

namespace download
{
	namespace
	{
		struct usermap_file
		{
			const char* key;
			const char* suffix;
			bool required;
		};

		// Files are always usermaps/<map>/<map><suffix>, never a server-provided path
		constexpr usermap_file usermap_files[] =
		{
			{"ff", ".ff", true},
			{"load", "_load.ff", false},
			{"pak", ".pak", false},
			{"arena", ".arena", false},
		};

		constexpr std::size_t max_map_name_length = 64;
		constexpr std::size_t max_base_url_length = 256;
		constexpr std::size_t max_challenge_length = 32;
		constexpr std::size_t max_request_length = 128;
		constexpr std::uint64_t max_total_download_size = 32ull * 1024 * 1024 * 1024;
		constexpr std::size_t sha256_hex_length = 64;

		constexpr auto manifest_resend_interval = 2s;
		constexpr auto manifest_timeout = 90s;
		// A slower check reconnects for fresh server info
		constexpr auto stale_info_age = 5s;
		constexpr auto download_attempts = 3;
		constexpr auto prompt_timeout = 2min;

		const game::dvar_t* sv_www_base_url{};
		const game::dvar_t* cl_allow_download{};

		std::atomic_bool shutting_down{};

		bool is_valid_map_name(const std::string& name)
		{
			return !name.empty() && name.size() <= max_map_name_length && name.starts_with("mp_")
				&& std::ranges::all_of(name, [](const unsigned char character)
				{
					return std::isalnum(character) != 0 || character == '_';
				});
		}

		bool is_hex(const std::string& value)
		{
			return std::ranges::all_of(value, [](const unsigned char character)
			{
				return std::isxdigit(character) != 0;
			});
		}

		bool is_valid_challenge(const std::string& value)
		{
			return !value.empty() && value.size() <= max_challenge_length
				&& std::ranges::all_of(value, [](const unsigned char character)
				{
					return std::isalnum(character) != 0;
				});
		}

		bool is_valid_base_url(const std::string& url)
		{
			if (url.empty() || url.size() > max_base_url_length)
			{
				return false;
			}

			const auto lower = utils::string::to_lower(url);
			if (!lower.starts_with("http://") && !lower.starts_with("https://"))
			{
				return false;
			}

			// No info string separators, whitespace or control characters
			return std::ranges::none_of(url, [](const unsigned char character)
			{
				return character <= ' ' || character == 0x7F || character == '\\' || character == '"';
			});
		}

		std::string get_url_host(const std::string& url)
		{
			const auto scheme = url.find("://");
			if (scheme == std::string::npos)
			{
				return url;
			}

			const auto start = scheme + 3;
			const auto end = url.find_first_of("/?#", start);
			return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
		}

		bool parse_size(const std::string& value, std::uint64_t& result)
		{
			if (value.empty() || value.size() > 20)
			{
				return false;
			}

			std::uint64_t parsed{};
			const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
			if (error != std::errc{} || end != value.data() + value.size())
			{
				return false;
			}

			result = parsed;
			return true;
		}

		std::string format_size(const std::uint64_t size)
		{
			if (size >= 1024ull * 1024 * 1024)
			{
				return std::format("{:.2f} GB", static_cast<double>(size) / (1024.0 * 1024.0 * 1024.0));
			}

			return std::format("{:.1f} MB", static_cast<double>(size) / (1024.0 * 1024.0));
		}

		std::string address_to_string(const game::netadr_s& address)
		{
			return network::net_adr_to_string(address);
		}

		bool is_matching_ip_endpoint(const game::netadr_s& lhs, const game::netadr_s& rhs)
		{
			const auto lhs_is_ip = lhs.type == game::NA_IP || lhs.type == game::NA_BROADCAST;
			const auto rhs_is_ip = rhs.type == game::NA_IP || rhs.type == game::NA_BROADCAST;
			return lhs_is_ip && rhs_is_ip && lhs.addr == rhs.addr && lhs.port == rhs.port;
		}

		struct file_stat
		{
			std::uint64_t size{};
			std::int64_t mtime{};
		};

		std::optional<file_stat> get_file_stat(const std::filesystem::path& path)
		{
			std::error_code error;
			if (!std::filesystem::is_regular_file(path, error) || error)
			{
				return {};
			}

			const auto size = std::filesystem::file_size(path, error);
			if (error)
			{
				return {};
			}

			const auto time = std::filesystem::last_write_time(path, error);
			if (error)
			{
				return {};
			}

			return file_stat{ size, static_cast<std::int64_t>(time.time_since_epoch().count()) };
		}

		enum class file_location
		{
			none,
			usermaps,
			zone,
		};

		struct resolved_file
		{
			std::filesystem::path path{};
			file_location location{ file_location::none };
		};

		// Same lookup order as fastfiles::find_custom_file, then the stock zone folder
		resolved_file resolve_file(const std::string& map_name, const std::string& filename)
		{
			const std::filesystem::path relative_usermap = std::filesystem::path("usermaps") / map_name / filename;
			const std::filesystem::path relative_zone = std::filesystem::path("zone") / filename;

			const auto check = [](const std::filesystem::path& path)
			{
				std::error_code error;
				return std::filesystem::is_regular_file(path, error) && !error;
			};

			for (const auto& search_path : filesystem::get_search_paths())
			{
				const std::filesystem::path root{ search_path };
				if (check(root / relative_zone))
				{
					return { root / relative_zone, file_location::zone };
				}

				if (check(root / relative_usermap))
				{
					return { root / relative_usermap, file_location::usermaps };
				}
			}

			const std::filesystem::path game_folder{ utils::nt::library{}.get_folder() };
			if (check(game_folder / relative_usermap))
			{
				return { game_folder / relative_usermap, file_location::usermaps };
			}

			if (check(game_folder / relative_zone))
			{
				return { game_folder / relative_zone, file_location::zone };
			}

			return {};
		}

		// Persisted so large files aren't rehashed on every join
		struct cached_hash
		{
			file_stat stat{};
			std::string sha256{};
		};

		struct hash_cache_t
		{
			bool loaded{};
			std::unordered_map<std::string, cached_hash> entries{};
		};

		utils::concurrency::container<hash_cache_t> hash_cache{};

		std::filesystem::path get_hash_cache_path()
		{
			return game::get_appdata_path() / "cache" / "file_hashes.txt";
		}

		std::string get_cache_key(const std::filesystem::path& path)
		{
			std::error_code error;
			auto absolute = std::filesystem::absolute(path, error);
			if (error)
			{
				absolute = path;
			}

			return utils::string::to_lower(absolute.lexically_normal().generic_string());
		}

		void load_hash_cache(hash_cache_t& cache)
		{
			if (cache.loaded)
			{
				return;
			}

			cache.loaded = true;

			std::string data{};
			if (!utils::io::read_file(get_hash_cache_path().generic_string(), &data))
			{
				return;
			}

			// <sha256> <size> <mtime> <path>
			std::istringstream stream{ data };
			std::string line{};
			while (std::getline(stream, line))
			{
				if (!line.empty() && line.back() == '\r')
				{
					line.pop_back();
				}

				const auto first = line.find(' ');
				const auto second = first == std::string::npos ? first : line.find(' ', first + 1);
				const auto third = second == std::string::npos ? second : line.find(' ', second + 1);
				if (third == std::string::npos)
				{
					continue;
				}

				cached_hash entry{};
				entry.sha256 = line.substr(0, first);
				const auto size = line.substr(first + 1, second - first - 1);
				const auto mtime = line.substr(second + 1, third - second - 1);
				const auto path = line.substr(third + 1);

				std::int64_t parsed_mtime{};
				const auto [end, error] = std::from_chars(mtime.data(), mtime.data() + mtime.size(), parsed_mtime);
				if (entry.sha256.size() != sha256_hex_length || !is_hex(entry.sha256)
					|| !parse_size(size, entry.stat.size)
					|| error != std::errc{} || end != mtime.data() + mtime.size() || path.empty())
				{
					continue;
				}

				entry.stat.mtime = parsed_mtime;
				cache.entries[path] = std::move(entry);
			}
		}

		void save_hash_cache(const hash_cache_t& cache)
		{
			std::string data{};
			for (const auto& [path, entry] : cache.entries)
			{
				data += std::format("{} {} {} {}\n", entry.sha256, entry.stat.size, entry.stat.mtime, path);
			}

			const auto path = get_hash_cache_path();
			utils::io::create_directory(path.parent_path());
			utils::io::write_file(path.generic_string(), data, false);
		}

		std::optional<std::string> get_cached_hash(const std::filesystem::path& path, const file_stat& stat)
		{
			const auto key = get_cache_key(path);
			return hash_cache.access<std::optional<std::string>>([&](hash_cache_t& cache) -> std::optional<std::string>
			{
				load_hash_cache(cache);
				const auto entry = cache.entries.find(key);
				if (entry == cache.entries.end() || entry->second.stat.size != stat.size
					|| entry->second.stat.mtime != stat.mtime)
				{
					return {};
				}

				return entry->second.sha256;
			});
		}

		void store_cached_hash(const std::filesystem::path& path, const file_stat& stat, const std::string& sha256)
		{
			const auto key = get_cache_key(path);
			hash_cache.access([&](hash_cache_t& cache)
			{
				load_hash_cache(cache);
				cache.entries[key] = cached_hash{ stat, sha256 };
				save_hash_cache(cache);
			});
		}

		using progress_callback = std::function<void(std::uint64_t done, std::uint64_t total)>;

		// Uppercase hex SHA-256, or nothing on read failure or cancel
		std::optional<std::string> hash_file(const std::filesystem::path& path, const std::atomic_bool& cancel,
			const progress_callback& progress = {})
		{
			const auto stat = get_file_stat(path);
			if (!stat)
			{
				return {};
			}

			if (auto cached = get_cached_hash(path, *stat))
			{
				return cached;
			}

			std::ifstream stream(path, std::ios::binary);
			if (!stream.is_open())
			{
				return {};
			}

			constexpr std::size_t buffer_size = 1024 * 1024;
			std::vector<char> buffer(buffer_size);
			utils::cryptography::sha256::hasher hasher{};

			std::uint64_t done = 0;
			while (done < stat->size)
			{
				if (cancel || shutting_down)
				{
					return {};
				}

				const auto read_size = static_cast<std::size_t>(std::min<std::uint64_t>(buffer_size, stat->size - done));
				stream.read(buffer.data(), static_cast<std::streamsize>(read_size));
				if (static_cast<std::size_t>(stream.gcount()) != read_size)
				{
					return {};
				}

				hasher.update(buffer.data(), read_size);
				done += read_size;

				if (progress)
				{
					progress(done, stat->size);
				}
			}

			stream.close();

			// Changed while reading
			const auto after = get_file_stat(path);
			if (!after || after->size != stat->size || after->mtime != stat->mtime)
			{
				return {};
			}

			auto hash = hasher.finish(true);
			store_cached_hash(path, *stat, hash);
			return hash;
		}

		struct manifest_entry
		{
			std::string key{};
			std::string filename{};
			std::uint64_t size{};
			std::string sha256{};
		};

		struct server_file
		{
			manifest_entry entry{};
			std::filesystem::path path{};
			file_stat stat{};
		};

		struct server_hash_job
		{
			std::string map_name{};
			std::atomic_bool cancel{};
		};

		struct server_manifest_t
		{
			std::string map_name{};
			bool ready{};
			bool failed{};
			std::vector<server_file> files{};
			std::shared_ptr<server_hash_job> job{};
		};

		utils::concurrency::container<server_manifest_t> server_manifest{};

		struct usermap_lookup_cache
		{
			std::string map_name{};
			bool result{};
			std::chrono::steady_clock::time_point time{};
		};

		usermap_lookup_cache last_usermap_lookup{};

		// Empty unless the .ff loads from a usermaps folder
		std::vector<server_file> collect_server_files(const std::string& map_name)
		{
			std::vector<server_file> files{};
			for (const auto& file : usermap_files)
			{
				const auto filename = map_name + file.suffix;
				const auto resolved = resolve_file(map_name, filename);
				if (resolved.location != file_location::usermaps)
				{
					if (file.required)
					{
						return {};
					}

					continue;
				}

				const auto stat = get_file_stat(resolved.path);
				if (!stat)
				{
					if (file.required)
					{
						return {};
					}

					continue;
				}

				server_file entry{};
				entry.entry.key = file.key;
				entry.entry.filename = filename;
				entry.entry.size = stat->size;
				entry.path = resolved.path;
				entry.stat = *stat;
				files.emplace_back(std::move(entry));
			}

			return files;
		}

		void start_server_hashing(server_manifest_t& manifest, const std::string& map_name)
		{
			if (manifest.job)
			{
				manifest.job->cancel = true;
			}

			auto job = std::make_shared<server_hash_job>();
			job->map_name = map_name;

			manifest = {};
			manifest.map_name = map_name;
			manifest.job = job;

			std::thread([job]
			{
				auto files = collect_server_files(job->map_name);
				auto failed = files.empty();

				for (auto& file : files)
				{
					if (failed || job->cancel)
					{
						break;
					}

					console::info("[Download] Hashing '%s' (%s)...\n", file.path.generic_string().data(),
						format_size(file.entry.size).data());

					const auto hash = hash_file(file.path, job->cancel);
					if (!hash)
					{
						failed = true;
						break;
					}

					file.entry.sha256 = *hash;
				}

				if (job->cancel)
				{
					return;
				}

				if (failed)
				{
					console::error("[Download] Unable to hash usermap '%s'; clients can't download it.\n",
						job->map_name.data());
				}
				else
				{
					console::info("[Download] Usermap '%s' is ready for download.\n", job->map_name.data());
				}

				server_manifest.access([&](server_manifest_t& manifest)
				{
					if (manifest.job != job)
					{
						return;
					}

					manifest.files = std::move(files);
					manifest.ready = !failed;
					manifest.failed = failed;
					manifest.job.reset();
				});
			}).detach();
		}

		void ensure_server_manifest(const std::string& map_name)
		{
			server_manifest.access([&](server_manifest_t& manifest)
			{
				if (manifest.map_name == map_name && (manifest.job || manifest.ready || manifest.failed))
				{
					return;
				}

				start_server_hashing(manifest, map_name);
			});
		}

		enum class manifest_status
		{
			ready,
			pending,
			unavailable,
		};

		manifest_status get_server_manifest(const std::string& map_name, std::vector<manifest_entry>& entries)
		{
			// Rehash files that changed on disk
			const auto current_files = collect_server_files(map_name);

			return server_manifest.access<manifest_status>([&](server_manifest_t& manifest)
			{
				if (manifest.map_name != map_name)
				{
					start_server_hashing(manifest, map_name);
					return manifest_status::pending;
				}

				if (manifest.job)
				{
					return manifest_status::pending;
				}

				const auto unchanged = current_files.size() == manifest.files.size()
					&& std::ranges::equal(current_files, manifest.files, [](const server_file& a, const server_file& b)
					{
						return a.entry.key == b.entry.key && a.path == b.path
							&& a.stat.size == b.stat.size && a.stat.mtime == b.stat.mtime;
					});

				if (!unchanged)
				{
					start_server_hashing(manifest, map_name);
					return manifest_status::pending;
				}

				if (!manifest.ready)
				{
					return manifest_status::unavailable;
				}

				for (const auto& file : manifest.files)
				{
					entries.emplace_back(file.entry);
				}

				return manifest_status::ready;
			});
		}

		std::string get_server_base_url()
		{
			if (!sv_www_base_url || !sv_www_base_url->current.string)
			{
				return {};
			}

			std::string url = sv_www_base_url->current.string;
			while (!url.empty() && url.back() == '/')
			{
				url.pop_back();
			}

			return url;
		}

		void handle_usermap_request(const game::netadr_s& from, std::string_view data)
		{
			while (!data.empty() && data.back() == '\0')
			{
				data.remove_suffix(1);
			}

			if (data.empty() || data.size() > max_request_length)
			{
				return;
			}

			// s2x_getUsermap <challenge> <mapname>
			const std::string request{ data };
			const auto separator = request.find(' ');
			if (separator == std::string::npos)
			{
				return;
			}

			const auto challenge = request.substr(0, separator);
			const auto map_name = request.substr(separator + 1);
			if (!is_valid_challenge(challenge) || !is_valid_map_name(map_name))
			{
				return;
			}

			utils::info_string info{};
			info.set("challenge", challenge);
			info.set("mapname", map_name);

			const auto base_url = get_server_base_url();
			std::vector<manifest_entry> entries{};

			if (party::get_advertised_map_name() != map_name || !is_server_usermap(map_name))
			{
				info.set("status", "unavailable");
			}
			else
			{
				const auto status = get_server_manifest(map_name, entries);
				info.set("status", status == manifest_status::ready
					? "ready"
					: status == manifest_status::pending ? "pending" : "unavailable");
			}

			if (!entries.empty())
			{
				// Clients that already have the map can join without a download URL
				if (is_valid_base_url(base_url))
				{
					info.set("base_url", base_url);
				}

				for (const auto& entry : entries)
				{
					info.set(entry.key + "_size", std::to_string(entry.size));
					info.set(entry.key + "_sha256", entry.sha256);
				}
			}

			network::send(from, "s2x_usermapResponse", info.build(), '\n');
		}

		enum class check_purpose
		{
			connect,
			rotation,
		};

		struct manifest_query_t
		{
			bool active{};
			check_purpose purpose{};
			game::netadr_s host{};
			std::string challenge{};
			std::string map_name{};
			utils::info_string info{};
			int max_clients{};
			std::uint64_t attempt_id{};
			std::chrono::steady_clock::time_point started{};
			std::chrono::steady_clock::time_point next_send{};
		};

		manifest_query_t manifest_query{};

		enum class job_result_type
		{
			verified,
			downloaded,
			needs_download,
			declined,
			failed,
			cancelled,
		};

		struct client_job
		{
			check_purpose purpose{};
			game::netadr_s host{};
			std::string host_string{};
			std::string map_name{};
			std::string base_url{};
			std::vector<manifest_entry> entries{};
			utils::info_string info{};
			int max_clients{};
			std::uint64_t attempt_id{};
			int allow_download{};
			std::chrono::steady_clock::time_point started{};

			std::atomic_bool cancel{};
			// prompt_answer
			std::atomic_int prompt{};
			std::weak_ptr<client_job> self{};
			job_result_type result{};
			std::string error{};
		};

		// One job at a time touches the files
		std::shared_ptr<client_job> running_job{};
		std::shared_ptr<client_job> queued_job{};

		// Last rotation map that passed the check
		struct rotation_state_t
		{
			game::netadr_s host{};
			std::string map_name{};
		};

		std::optional<rotation_state_t> verified_rotation{};

		void clear_manifest_query()
		{
			manifest_query = {};
		}

		void send_manifest_query()
		{
			manifest_query.next_send = std::chrono::steady_clock::now() + manifest_resend_interval;
			network::send(manifest_query.host, "s2x_getUsermap",
				manifest_query.challenge + " " + manifest_query.map_name);
		}

		void start_manifest_query(const check_purpose purpose, const game::netadr_s& host,
			const std::string& map_name, const utils::info_string& info, const int max_clients,
			const std::uint64_t attempt_id)
		{
			manifest_query = {};
			manifest_query.active = true;
			manifest_query.purpose = purpose;
			manifest_query.host = host;
			manifest_query.challenge = utils::cryptography::random::get_challenge();
			manifest_query.map_name = map_name;
			manifest_query.info = info;
			manifest_query.max_clients = max_clients;
			manifest_query.attempt_id = attempt_id;
			manifest_query.started = std::chrono::steady_clock::now();
			send_manifest_query();
		}

		std::filesystem::path get_trusted_servers_path()
		{
			return game::get_appdata_path() / "download_trusted_servers.txt";
		}

		std::mutex trusted_servers_mutex;

		bool is_trusted_server(const std::string& host)
		{
			std::lock_guard _(trusted_servers_mutex);
			std::string data{};
			if (!utils::io::read_file(get_trusted_servers_path().generic_string(), &data))
			{
				return false;
			}

			std::istringstream stream{ data };
			std::string line{};
			while (std::getline(stream, line))
			{
				if (!line.empty() && line.back() == '\r')
				{
					line.pop_back();
				}

				if (line == host)
				{
					return true;
				}
			}

			return false;
		}

		void add_trusted_server(const std::string& host)
		{
			std::lock_guard _(trusted_servers_mutex);
			utils::io::write_file(get_trusted_servers_path().generic_string(), host + "\n", true);
		}

		// Menus live in data/ui_scripts/mp/download
		struct progress_state_t
		{
			bool active{};
			std::string map_name{};
			std::string host{};
			std::string file{};
			std::uint64_t done{};
			std::uint64_t total{};
			std::uint64_t file_done{};
			std::uint64_t file_total{};
		};

		utils::concurrency::container<progress_state_t> progress_state{};

		// Set by the script; without it errors go to the console only
		bool is_download_ui_ready()
		{
			if (!*game::hks::lui_lua_state)
			{
				return false;
			}

			try
			{
				const auto value = ui_scripting::get_globals().get("s2x_download_ui");
				return value.is<bool>() && value.as<bool>();
			}
			catch (const std::exception&)
			{
				return false;
			}
		}

		bool notify_ui(const std::string& name, const ui_scripting::event_arguments& arguments = {})
		{
			return is_download_ui_ready() && ui_scripting::notify(name, arguments);
		}

		void show_connect_error(const std::string& message)
		{
			console::error("Connection failed: %s\n", message.data());
			notify_ui("s2x_download_error", { { "message", message.data() } });
		}

		enum class prompt_answer : int
		{
			pending,
			accepted,
			declined,
			unavailable,
			cancelled,
		};

		struct prompt_request_t
		{
			int id{};
			std::weak_ptr<client_job> job{};
		};

		// Main thread only
		prompt_request_t prompt_request{};
		int next_prompt_id{};

		prompt_answer ask_download_permission(client_job& job, const std::uint64_t total_size)
		{
			job.prompt = static_cast<int>(prompt_answer::pending);

			const auto message = std::format(
				"The server is running the custom map '{}', which you don't have or have a different version of.\n\n"
				"Download {} from {}?",
				job.map_name, format_size(total_size), get_url_host(job.base_url));

			scheduler::once([weak = job.self, message]
			{
				const auto job = weak.lock();
				if (!job)
				{
					return;
				}

				prompt_request = { ++next_prompt_id, weak };
				if (!notify_ui("s2x_download_prompt", {
					// 64-bit values reach Lua as hex strings
					{ "id", static_cast<int>(prompt_request.id) },
					{ "message", message.data() },
				}))
				{
					prompt_request = {};
					job->prompt = static_cast<int>(prompt_answer::unavailable);
				}
			}, scheduler::pipeline::main);

			const auto deadline = std::chrono::steady_clock::now() + prompt_timeout;
			while (job.prompt == static_cast<int>(prompt_answer::pending))
			{
				if (job.cancel || shutting_down || std::chrono::steady_clock::now() >= deadline)
				{
					return prompt_answer::cancelled;
				}

				std::this_thread::sleep_for(100ms);
			}

			return static_cast<prompt_answer>(job.prompt.load());
		}

		void answer_prompt(const int id, const bool accepted)
		{
			if (id != prompt_request.id)
			{
				return;
			}

			if (const auto job = prompt_request.job.lock())
			{
				job->prompt = static_cast<int>(accepted ? prompt_answer::accepted : prompt_answer::declined);
			}

			prompt_request = {};
		}

		struct pending_download
		{
			manifest_entry entry{};
			std::filesystem::path target{};
		};

		bool download_file(client_job& job, const pending_download& download, std::uint64_t& total_done)
		{
			const auto& entry = download.entry;
			auto part_path = download.target;
			part_path += ".part";

			const auto url = job.base_url + "/usermaps/" + job.map_name + "/" + entry.filename;
			console::info("[Download] Downloading %s (%s)\n", url.data(), format_size(entry.size).data());

			std::error_code error;
			std::filesystem::create_directories(download.target.parent_path(), error);

			const auto base_done = total_done;
			auto last_update = std::chrono::steady_clock::now();
			auto last_console = last_update;

			for (auto attempt = 0; attempt < download_attempts; ++attempt)
			{
				// Resume a partial file; rehash its bytes so the hash covers the whole file
				std::uint64_t resume_from = 0;
				if (const auto part_stat = get_file_stat(part_path); part_stat && part_stat->size < entry.size)
				{
					resume_from = part_stat->size;
				}
				else if (part_stat)
				{
					utils::io::remove_file(part_path);
				}

				std::optional<utils::cryptography::sha256::hasher> hasher{};
				hasher.emplace();
				if (resume_from > 0)
				{
					std::ifstream existing(part_path, std::ios::binary);
					std::vector<char> buffer(1024 * 1024);
					std::uint64_t read_total = 0;
					while (existing && read_total < resume_from)
					{
						const auto read_size = static_cast<std::size_t>(
							std::min<std::uint64_t>(buffer.size(), resume_from - read_total));
						existing.read(buffer.data(), static_cast<std::streamsize>(read_size));
						const auto got = static_cast<std::size_t>(existing.gcount());
						if (got == 0)
						{
							break;
						}

						hasher->update(buffer.data(), got);
						read_total += got;
					}

					if (read_total != resume_from)
					{
						utils::io::remove_file(part_path);
						resume_from = 0;
						hasher.emplace();
					}
					else
					{
						console::info("[Download] Resuming '%s' at %s\n", entry.filename.data(),
							format_size(resume_from).data());
					}
				}

				std::ofstream output(part_path, std::ios::binary | (resume_from > 0 ? std::ios::app : std::ios::trunc));
				if (!output.is_open())
				{
					job.error = "Unable to write '" + part_path.generic_string() + "'.";
					return false;
				}

				std::uint64_t received = resume_from;
				auto too_large = false;
				auto write_failed = false;

				const auto result = utils::http::download(url, resume_from, [&](const char* data, const size_t size)
				{
					if (received + size > entry.size)
					{
						too_large = true;
						return false;
					}

					output.write(data, static_cast<std::streamsize>(size));
					if (!output)
					{
						write_failed = true;
						return false;
					}

					hasher->update(data, size);
					received += size;
					total_done = base_done + received;

					const auto now = std::chrono::steady_clock::now();
					if (now - last_update > 100ms)
					{
						last_update = now;
						progress_state.access([&](progress_state_t& progress)
						{
							progress.file = entry.filename;
							progress.done = total_done;
							progress.file_done = received;
							progress.file_total = entry.size;
						});
					}

					if (now - last_console > 5s)
					{
						last_console = now;
						console::info("[Download] %s: %s / %s\n", entry.filename.data(),
							format_size(received).data(), format_size(entry.size).data());
					}

					return true;
				}, [&]
				{
					return !job.cancel && !shutting_down;
				});

				output.close();

				if (job.cancel || shutting_down)
				{
					job.result = job_result_type::cancelled;
					job.error = "Download cancelled.";
					return false;
				}

				if (too_large)
				{
					utils::io::remove_file(part_path);
					job.error = "The server sent more data for '" + entry.filename + "' than its manifest lists.";
					return false;
				}

				if (write_failed)
				{
					job.error = "Unable to write '" + part_path.generic_string() + "' (disk full?).";
					return false;
				}

				if (result.range_ignored)
				{
					// No resume support; start over
					utils::io::remove_file(part_path);
					continue;
				}

				if (!result.success)
				{
					console::warn("[Download] '%s' failed: %s\n", entry.filename.data(), result.error.data());
					job.error = "Download of '" + entry.filename + "' failed: " + result.error;

					// Client errors won't change on retry
					if (result.http_code >= 400 && result.http_code < 500
						&& result.http_code != 408 && result.http_code != 429)
					{
						return false;
					}

					continue;
				}

				if (received != entry.size)
				{
					job.error = "'" + entry.filename + "' is " + std::to_string(received)
						+ " bytes instead of the " + std::to_string(entry.size) + " the server lists.";
					continue;
				}

				const auto hash = hasher->finish(true);
				if (_stricmp(hash.data(), entry.sha256.data()) != 0)
				{
					utils::io::remove_file(part_path);
					job.error = "'" + entry.filename + "' doesn't match the server's hash (got " + hash
						+ ", expected " + entry.sha256 + ").";
					return false;
				}

				if (!MoveFileExW(part_path.wstring().data(), download.target.wstring().data(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				{
					job.error = std::format("Unable to replace '{}' (error {}). Close anything using it and retry.",
						download.target.generic_string(), GetLastError());
					return false;
				}

				if (const auto stat = get_file_stat(download.target))
				{
					store_cached_hash(download.target, *stat, utils::string::to_upper(entry.sha256));
				}

				console::info("[Download] Saved '%s'\n", download.target.generic_string().data());
				job.error.clear();
				return true;
			}

			if (job.error.empty())
			{
				job.error = "Download of '" + entry.filename + "' failed.";
			}

			return false;
		}

		void run_client_job(client_job& job)
		{
			std::vector<pending_download> downloads{};
			std::filesystem::path target_directory = std::filesystem::path(utils::nt::library{}.get_folder())
				/ "usermaps" / job.map_name;

			if (const auto ff = resolve_file(job.map_name, job.map_name + ".ff");
				ff.location == file_location::usermaps)
			{
				target_directory = ff.path.parent_path();
			}

			for (const auto& entry : job.entries)
			{
				if (job.cancel || shutting_down)
				{
					job.result = job_result_type::cancelled;
					return;
				}

				const auto resolved = resolve_file(job.map_name, entry.filename);
				if (resolved.location != file_location::none)
				{
					const auto stat = get_file_stat(resolved.path);
					if (stat && stat->size == entry.size)
					{
						if (!get_cached_hash(resolved.path, *stat))
						{
							console::info("[Download] Checking '%s' (%s)...\n",
								resolved.path.generic_string().data(), format_size(entry.size).data());
						}

						const auto hash = hash_file(resolved.path, job.cancel);
						if (job.cancel || shutting_down)
						{
							job.result = job_result_type::cancelled;
							return;
						}

						if (hash && _stricmp(hash->data(), entry.sha256.data()) == 0)
						{
							continue;
						}
					}

					if (resolved.location == file_location::zone)
					{
						job.result = job_result_type::failed;
						job.error = "'" + resolved.path.generic_string()
							+ "' differs from the server's copy. Remove it so the map can be downloaded to usermaps.";
						return;
					}

					downloads.emplace_back(pending_download{ entry, resolved.path });
					continue;
				}

				downloads.emplace_back(pending_download{ entry, target_directory / entry.filename });
			}

			if (downloads.empty())
			{
				job.result = job_result_type::verified;
				return;
			}

			std::uint64_t total_size = 0;
			for (const auto& download : downloads)
			{
				total_size += download.entry.size;
			}

			if (job.base_url.empty())
			{
				job.result = job_result_type::failed;
				job.error = "This server runs the custom map '" + job.map_name + "' (" + format_size(total_size)
					+ ") but has no download URL (sv_wwwBaseUrl).";
				return;
			}

			if (job.purpose == check_purpose::rotation)
			{
				job.result = job_result_type::needs_download;
				return;
			}

			if (job.allow_download == 0)
			{
				job.result = job_result_type::failed;
				job.error = "Map '" + job.map_name + "' needs a " + format_size(total_size)
					+ " download, but cl_allowDownload is 0.";
				return;
			}

			if (job.allow_download == 1 && !is_trusted_server(job.host_string))
			{
				switch (ask_download_permission(job, total_size))
				{
				case prompt_answer::accepted:
					add_trusted_server(job.host_string);
					break;
				case prompt_answer::declined:
					job.result = job_result_type::declined;
					return;
				case prompt_answer::unavailable:
					job.result = job_result_type::failed;
					job.error = "Map '" + job.map_name + "' needs a " + format_size(total_size)
						+ " download, but the download menu isn't available. Join from the menu, "
						"or set cl_allowDownload 2 to download without asking.";
					return;
				default:
					job.result = job_result_type::cancelled;
					return;
				}
			}

			if (job.cancel || shutting_down)
			{
				job.result = job_result_type::cancelled;
				return;
			}

			ULARGE_INTEGER free_bytes{};
			std::error_code error;
			std::filesystem::create_directories(target_directory, error);
			if (GetDiskFreeSpaceExW(target_directory.wstring().data(), &free_bytes, nullptr, nullptr)
				&& free_bytes.QuadPart < total_size)
			{
				job.result = job_result_type::failed;
				job.error = "Not enough disk space for '" + job.map_name + "' (" + format_size(total_size)
					+ " needed, " + format_size(free_bytes.QuadPart) + " free).";
				return;
			}

			progress_state.access([&](progress_state_t& progress)
			{
				progress = {};
				progress.active = true;
				progress.map_name = job.map_name;
				progress.host = get_url_host(job.base_url);
				progress.total = total_size;
			});

			const auto _ = utils::finally([]
			{
				progress_state.access([](progress_state_t& progress)
				{
					progress.active = false;
				});
			});

			scheduler::once([]
			{
				notify_ui("s2x_download_start");
			}, scheduler::pipeline::main);

			console::info("[Download] Downloading map '%s' (%s in %zu files)\n", job.map_name.data(),
				format_size(total_size).data(), downloads.size());

			std::uint64_t total_done = 0;
			for (const auto& download : downloads)
			{
				const auto file_start = total_done;
				if (!download_file(job, download, total_done))
				{
					if (job.result != job_result_type::cancelled)
					{
						job.result = job_result_type::failed;
					}

					return;
				}

				total_done = file_start + download.entry.size;
			}

			job.result = job_result_type::downloaded;
		}

		void finish_client_job(const std::shared_ptr<client_job>& job)
		{
			if (running_job == job)
			{
				running_job.reset();
			}

			if (job->purpose == check_purpose::rotation)
			{
				const auto still_joined = party_client::has_dedicated_session(game::Lobby_GetPartyData(0))
					&& is_matching_ip_endpoint(party::get_target(), job->host);
				if (!still_joined || job->cancel)
				{
					return;
				}

				if (job->result == job_result_type::verified)
				{
					verified_rotation = rotation_state_t{ job->host, job->map_name };
					return;
				}

				if (job->result == job_result_type::needs_download)
				{
					console::info("[Download] The next map '%s' needs a download. Leaving the lobby to get it.\n",
						job->map_name.data());

					const auto host = job->host;
					game::Cbuf_AddText(0, "disconnect\n");
					scheduler::once([host]
					{
						party::connect_to(host);
					}, scheduler::pipeline::main, 1s);
					return;
				}

				if (job->result == job_result_type::failed && !job->error.empty())
				{
					console::warn("[Download] Unable to check the next map '%s': %s\n",
						job->map_name.data(), job->error.data());
				}

				return;
			}

			if (!party::is_connection_attempt_current(job->attempt_id))
			{
				return;
			}

			switch (job->result)
			{
			case job_result_type::verified:
				if (std::chrono::steady_clock::now() - job->started > stale_info_age)
				{
					// Info may be stale; the recheck hits the hash cache
					party::retry_connect(job->host, job->attempt_id);
					return;
				}

				party::continue_connect(job->host, job->info, job->max_clients, job->attempt_id);
				return;
			case job_result_type::downloaded:
				console::info("[Download] Map '%s' downloaded. Reconnecting...\n", job->map_name.data());
				party::retry_connect(job->host, job->attempt_id);
				return;
			case job_result_type::declined:
				console::info("[Download] Download of map '%s' declined.\n", job->map_name.data());
				return;
			case job_result_type::cancelled:
				console::info("[Download] Download of map '%s' cancelled.\n", job->map_name.data());
				return;
			case job_result_type::failed:
			case job_result_type::needs_download:
				show_connect_error(job->error.empty() ? "map download failed." : job->error);
				return;
			}
		}

		void start_client_job(const std::shared_ptr<client_job>& job)
		{
			job->self = job;
			running_job = job;
			std::thread([job]
			{
				try
				{
					run_client_job(*job);
				}
				catch (const std::exception& e)
				{
					job->result = job_result_type::failed;
					job->error = e.what();
				}

				scheduler::once([job]
				{
					finish_client_job(job);
				}, scheduler::pipeline::main);
			}).detach();
		}

		void queue_client_job(const std::shared_ptr<client_job>& job)
		{
			if (running_job)
			{
				running_job->cancel = true;
				if (queued_job)
				{
					queued_job->cancel = true;
				}

				queued_job = job;
				return;
			}

			start_client_job(job);
		}

		bool is_job_obsolete(const client_job& job)
		{
			if (job.purpose == check_purpose::connect)
			{
				return !party::is_connection_attempt_current(job.attempt_id);
			}

			return !party_client::has_dedicated_session(game::Lobby_GetPartyData(0));
		}

		// Without a manifest, join unchecked if the map is installed
		void continue_without_manifest(const std::string& reason)
		{
			const auto query = manifest_query;
			clear_manifest_query();
			if (query.purpose != check_purpose::connect)
			{
				return;
			}

			if (resolve_file(query.map_name, query.map_name + ".ff").location == file_location::none)
			{
				show_connect_error(reason);
				return;
			}

			console::warn("[Download] %s Joining with the local copy unchecked.\n", reason.data());
			party::continue_connect(query.host, query.info, query.max_clients, query.attempt_id);
		}

		void run_client_frame()
		{
			if (running_job && !running_job->cancel && is_job_obsolete(*running_job))
			{
				running_job->cancel = true;
			}

			if (queued_job && is_job_obsolete(*queued_job))
			{
				queued_job.reset();
			}

			if (!running_job && queued_job)
			{
				auto job = std::move(queued_job);
				queued_job.reset();
				start_client_job(job);
			}

			if (!manifest_query.active)
			{
				return;
			}

			if (manifest_query.purpose == check_purpose::connect
				&& !party::is_connection_attempt_current(manifest_query.attempt_id))
			{
				clear_manifest_query();
				return;
			}

			const auto now = std::chrono::steady_clock::now();
			if (now - manifest_query.started > manifest_timeout)
			{
				continue_without_manifest("The server didn't provide the map '" + manifest_query.map_name + "' in time.");
				return;
			}

			if (now >= manifest_query.next_send)
			{
				send_manifest_query();
			}
		}

		bool parse_manifest(const utils::info_string& info, const std::string& map_name,
			std::vector<manifest_entry>& entries, std::string& error)
		{
			std::uint64_t total_size = 0;
			for (const auto& file : usermap_files)
			{
				const auto size_value = info.get(std::string(file.key) + "_size");
				const auto hash_value = info.get(std::string(file.key) + "_sha256");
				if (size_value.empty() && hash_value.empty())
				{
					if (file.required)
					{
						error = "the server's map manifest has no fastfile.";
						return false;
					}

					continue;
				}

				manifest_entry entry{};
				entry.key = file.key;
				entry.filename = map_name + file.suffix;
				entry.sha256 = hash_value;

				if (!parse_size(size_value, entry.size) || entry.size == 0 || entry.size > max_total_download_size
					|| entry.sha256.size() != sha256_hex_length || !is_hex(entry.sha256))
				{
					error = "the server's map manifest is invalid.";
					return false;
				}

				total_size += entry.size;
				if (total_size > max_total_download_size)
				{
					error = "the server's map is larger than " + format_size(max_total_download_size) + ".";
					return false;
				}

				entries.emplace_back(std::move(entry));
			}

			return true;
		}

		void handle_usermap_response(const game::netadr_s& from, const std::string_view& data)
		{
			if (game::environment::is_dedicated() || !manifest_query.active)
			{
				return;
			}

			const utils::info_string info{ std::string{ data } };
			if (!is_matching_ip_endpoint(from, manifest_query.host)
				|| info.get("challenge") != manifest_query.challenge
				|| info.get("mapname") != manifest_query.map_name)
			{
				return;
			}

			if (manifest_query.purpose == check_purpose::connect
				&& !party::is_connection_attempt_current(manifest_query.attempt_id))
			{
				clear_manifest_query();
				return;
			}

			const auto is_connect = manifest_query.purpose == check_purpose::connect;
			const auto status = info.get("status");
			if (status == "pending")
			{
				console::info("[Download] Waiting for the server to prepare map '%s'...\n",
					manifest_query.map_name.data());
				return;
			}

			if (status != "ready")
			{
				continue_without_manifest("The server can't provide its custom map '" + manifest_query.map_name + "'.");
				return;
			}

			std::vector<manifest_entry> entries{};
			std::string error{};
			const auto base_url = info.get("base_url");
			if (!base_url.empty() && !is_valid_base_url(base_url))
			{
				error = "the server's download URL is invalid.";
			}

			if (!error.empty() || !parse_manifest(info, manifest_query.map_name, entries, error))
			{
				if (is_connect)
				{
					show_connect_error(error);
				}

				clear_manifest_query();
				return;
			}

			auto job = std::make_shared<client_job>();
			job->purpose = manifest_query.purpose;
			job->host = manifest_query.host;
			job->host_string = address_to_string(manifest_query.host);
			job->map_name = manifest_query.map_name;
			job->base_url = base_url;
			while (!job->base_url.empty() && job->base_url.back() == '/')
			{
				job->base_url.pop_back();
			}
			job->entries = std::move(entries);
			job->info = manifest_query.info;
			job->max_clients = manifest_query.max_clients;
			job->attempt_id = manifest_query.attempt_id;
			job->allow_download = cl_allow_download ? cl_allow_download->current.integer : 1;
			job->started = manifest_query.started;

			clear_manifest_query();
			queue_client_job(job);
		}

		void install_lua_api()
		{
			const auto lua = ui_scripting::get_globals();
			ui_scripting::table download_table{};
			lua["s2x_download"] = download_table;

			download_table["respond"] = [](const int id, const bool accepted)
			{
				answer_prompt(id, accepted);
			};

			download_table["abort"] = []()
			{
				if (running_job)
				{
					running_job->cancel = true;
				}
			};

			download_table["getprogress"] = []()
			{
				const auto progress = progress_state.copy();
				ui_scripting::table result{};
				result["active"] = progress.active;
				result["map"] = progress.map_name;
				result["host"] = progress.host;
				result["file"] = progress.file;
				result["done"] = static_cast<double>(progress.done);
				result["total"] = static_cast<double>(progress.total);
				result["filedone"] = static_cast<double>(progress.file_done);
				result["filetotal"] = static_cast<double>(progress.file_total);
				return result;
			};
		}

		std::string get_info_map_name(const utils::info_string& info)
		{
			return info.get("party_session").empty() ? info.get("mapname") : info.get("party_mapname");
		}
	}

	bool is_server_usermap(const std::string& map_name)
	{
		if (!is_valid_map_name(map_name))
		{
			return false;
		}

		// getInfo is queried often; cache the lookup
		const auto now = std::chrono::steady_clock::now();
		if (last_usermap_lookup.map_name == map_name && now - last_usermap_lookup.time < 5s)
		{
			return last_usermap_lookup.result;
		}

		const auto resolved = resolve_file(map_name, map_name + ".ff");
		const auto result = resolved.location == file_location::usermaps;
		last_usermap_lookup = { map_name, result, now };

		if (result)
		{
			ensure_server_manifest(map_name);
		}

		return result;
	}

	bool begin_connect_check(const game::netadr_s& from, const utils::info_string& info,
		const int max_clients, const std::uint64_t attempt_id)
	{
		if (game::environment::is_dedicated())
		{
			return false;
		}

		const auto usermap = info.get("usermap");
		if (usermap.empty())
		{
			return false;
		}

		const auto map_name = get_info_map_name(info);
		if (usermap != map_name || !is_valid_map_name(usermap))
		{
			show_connect_error("invalid usermap '" + usermap + "'.");
			return true;
		}

		console::info("[Download] Server runs custom map '%s'; checking local files...\n", usermap.data());
		start_manifest_query(check_purpose::connect, from, usermap, info, max_clients, attempt_id);
		return true;
	}

	void check_rotation_map(const game::netadr_s& from, const utils::info_string& info)
	{
		if (game::environment::is_dedicated())
		{
			return;
		}

		const auto usermap = info.get("usermap");
		if (usermap.empty() || usermap != info.get("party_mapname") || !is_valid_map_name(usermap))
		{
			return;
		}

		if (verified_rotation && is_matching_ip_endpoint(verified_rotation->host, from)
			&& verified_rotation->map_name == usermap)
		{
			return;
		}

		if (manifest_query.active || running_job || queued_job)
		{
			return;
		}

		start_manifest_query(check_purpose::rotation, from, usermap, info, 0, 0);
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			scheduler::once([]
			{
				sv_www_base_url = game::Dvar_RegisterString("sv_wwwBaseUrl", "", game::DVAR_FLAG_NONE);

				if (!game::environment::is_dedicated())
				{
					// 0: never, 1: ask once per server, 2: always
					cl_allow_download = game::Dvar_RegisterInt("cl_allowDownload", 1, 0, 2, game::DVAR_FLAG_SAVED);
				}
			}, scheduler::pipeline::main);

			network::on("s2x_getUsermap", [](const game::netadr_s& from, const std::string_view& data)
			{
				handle_usermap_request(from, data);
			});

			if (!game::environment::is_dedicated())
			{
				network::on("s2x_usermapResponse", handle_usermap_response);
				ui_scripting::on_start(install_lua_api);
				scheduler::loop(run_client_frame, scheduler::pipeline::main, 100ms);
			}
		}

		void pre_destroy() override
		{
			shutting_down = true;
		}
	};
}

REGISTER_COMPONENT(download::component)
