#pragma once

#include <string>
#include <optional>
#include <future>
#include <unordered_map>

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
}
