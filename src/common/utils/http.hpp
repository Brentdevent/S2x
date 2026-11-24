#pragma once

#include <string>
#include <optional>
#include <future>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace utils::http
{
	using headers = std::unordered_map<std::string, std::string>;

	// Try the host normally first, then fallback_connect_to (host:port:IP:port) if the request fails.
	// validate may throw std::runtime_error to reject a response and try the fallback.
	std::optional<std::string> get_data(const std::string& url, const headers& headers = {},
		const std::function<void(size_t)>& callback = {}, uint32_t retries = 2,
		const std::string& fallback_connect_to = {},
		const std::function<void(const std::string&)>& validate = {});
	std::future<std::optional<std::string>> get_data_async(const std::string& url, const headers& headers = {});

	struct download_result
	{
		bool success{};
		// The server ignored the Range request
		bool range_ignored{};
		long http_code{};
		std::string error{};
	};

	// Streams to on_data; resume_from > 0 sends a Range request. Callbacks return false to abort.
	download_result download(const std::string& url, std::uint64_t resume_from,
		const std::function<bool(const char* data, size_t size)>& on_data,
		const std::function<bool()>& should_continue = {});
}
