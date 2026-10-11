#include "http.hpp"
#include <chrono>
#include <curl/curl.h>
#include <thread>
#include "finally.hpp"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Secur32.lib")

namespace utils::http
{
	namespace
	{
		bool can_try_fallback(const CURLcode result)
		{
			switch (result)
			{
			case CURLE_COULDNT_RESOLVE_HOST:
			case CURLE_COULDNT_CONNECT:
			case CURLE_PROXY:
			case CURLE_OPERATION_TIMEDOUT:
			case CURLE_SSL_CONNECT_ERROR:
			case CURLE_PEER_FAILED_VERIFICATION:
			case CURLE_SSL_ISSUER_ERROR:
			case CURLE_SSL_INVALIDCERTSTATUS:
			case CURLE_HTTP_RETURNED_ERROR:
			case CURLE_TOO_MANY_REDIRECTS:
			case CURLE_GOT_NOTHING:
			case CURLE_SEND_ERROR:
			case CURLE_RECV_ERROR:
			case CURLE_PARTIAL_FILE:
				return true;
			default:
				return false;
			}
		}

		struct progress_helper
		{
			const std::function<void(size_t)>* callback{};
			std::exception_ptr exception{};
		};

		int progress_callback(void* clientp, const curl_off_t /*dltotal*/, const curl_off_t dlnow,
		                      const curl_off_t /*ultotal*/, const curl_off_t /*ulnow*/)
		{
			auto* helper = static_cast<progress_helper*>(clientp);

			try
			{
				if (*helper->callback)
				{
					(*helper->callback)(dlnow);
				}
			}
			catch (...)
			{
				helper->exception = std::current_exception();
				return -1;
			}

			return 0;
		}

		size_t write_callback(void* contents, const size_t size, const size_t nmemb, void* userp)
		{
			auto* buffer = static_cast<std::string*>(userp);

			const auto total_size = size * nmemb;
			buffer->append(static_cast<char*>(contents), total_size);
			return total_size;
		}
	}

	std::optional<std::string> get_data(const std::string& url, const headers& headers,
	                                    const std::function<void(size_t)>& callback, const uint32_t retries,
	                                    const std::string& fallback_connect_to,
	                                    const std::function<void(const std::string&)>& validate)
	{
		curl_slist* header_list = nullptr;
		curl_slist* connect_list = nullptr;
		auto* curl = curl_easy_init();
		if (!curl)
		{
			return {};
		}

		auto _ = utils::finally([&]()
		{
			curl_easy_cleanup(curl);
			curl_slist_free_all(header_list);
			curl_slist_free_all(connect_list);
		});

		for (const auto& header : headers)
		{
			auto data = header.first + ": " + header.second;
			header_list = curl_slist_append(header_list, data.data());
		}

		std::string buffer{};
		progress_helper helper{};
		helper.callback = &callback;

		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
		curl_easy_setopt(curl, CURLOPT_URL, url.data());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &helper);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "s2x-updater/1.0");
		curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);

		for (auto retry_count = 0u;;)
		{
			buffer.clear();

			const auto result = curl_easy_perform(curl);
			if (helper.exception)
			{
				std::rethrow_exception(helper.exception);
			}

			long http_code = 0;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
			std::exception_ptr validation_error{};
			if (result == CURLE_OK && http_code >= 200 && http_code < 300)
			{
				try
				{
					if (validate)
					{
						validate(buffer);
					}
					return {std::move(buffer)};
				}
				catch (const std::runtime_error&)
				{
					validation_error = std::current_exception();
				}
			}

			if (!fallback_connect_to.empty() && !connect_list && (result == CURLE_OK || can_try_fallback(result)))
			{
				connect_list = curl_slist_append(nullptr, fallback_connect_to.data());
				if (!connect_list || curl_easy_setopt(curl, CURLOPT_CONNECT_TO, connect_list) != CURLE_OK
					|| curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L) != CURLE_OK)
				{
					return {};
				}

				// Route direct and proxied connections to the fallback, retaining Host and TLS checks.
				// Never reuse a block-page connection.
				// The fallback gets one immediate attempt even with ordinary retries disabled.
				continue;
			}

			if (validation_error)
			{
				std::rethrow_exception(validation_error);
			}
			if (result == CURLE_OK)
			{
				throw std::runtime_error(
					"Bad status code " + std::to_string(http_code) + " met while trying to download file " + url);
			}

			// Retry interrupted transfers and temporary server errors, including after headers arrived.
			if (http_code >= 400 && http_code < 500 && http_code != 408 && http_code != 429)
			{
				break;
			}

			if (retry_count == retries)
			{
				break;
			}
			++retry_count;
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}

		return {};
	}

	std::future<std::optional<std::string>> get_data_async(const std::string& url, const headers& headers)
	{
		return std::async(std::launch::async, [url, headers]()
		{
			return get_data(url, headers);
		});
	}
}
