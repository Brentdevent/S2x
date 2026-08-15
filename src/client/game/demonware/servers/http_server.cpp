#include <std_include.hpp>
#include "../dw_include.hpp"

#include "http_server.hpp"

#include <utils/string.hpp>

#include <charconv>

namespace demonware
{
	namespace
	{
		std::string trim(const std::string_view value)
		{
			const auto begin = value.find_first_not_of(" \t");
			if (begin == std::string_view::npos)
			{
				return {};
			}

			const auto end = value.find_last_not_of(" \t");
			return std::string{value.substr(begin, end - begin + 1)};
		}

		std::string normalize_header_name(const std::string_view name)
		{
			std::string result{name};
			std::transform(result.begin(), result.end(), result.begin(), [](const unsigned char character)
			{
				return static_cast<char>(std::tolower(character));
			});
			return result;
		}

		bool parse_request_head(const std::string_view head, http_request& request)
		{
			const auto request_line_end = head.find("\r\n");
			const auto request_line = head.substr(0, request_line_end);
			const auto method_end = request_line.find(' ');
			if (method_end == std::string_view::npos)
			{
				return false;
			}

			const auto target_end = request_line.find(' ', method_end + 1);
			if (target_end == std::string_view::npos)
			{
				return false;
			}

			request.method.assign(request_line.substr(0, method_end));
			request.target.assign(request_line.substr(method_end + 1, target_end - method_end - 1));

			auto line_begin = request_line_end == std::string_view::npos
				? head.size()
				: request_line_end + 2;

			while (line_begin < head.size())
			{
				const auto line_end = head.find("\r\n", line_begin);
				const auto line = head.substr(line_begin,
					line_end == std::string_view::npos ? head.size() - line_begin : line_end - line_begin);
				const auto separator = line.find(':');
				if (separator != std::string_view::npos)
				{
					request.headers[normalize_header_name(line.substr(0, separator))] =
						trim(line.substr(separator + 1));
				}

				if (line_end == std::string_view::npos)
				{
					break;
				}

				line_begin = line_end + 2;
			}

			return true;
		}

		bool parse_content_length(const http_request& request, std::size_t& length)
		{
			const auto entry = request.headers.find("content-length");
			if (entry == request.headers.end())
			{
				return false;
			}

			const auto* first = entry->second.data();
			const auto* last = first + entry->second.size();
			const auto result = std::from_chars(first, last, length);
			return result.ec == std::errc{} && result.ptr == last;
		}

	}

	void http_server::handle(const SOCKET socket, const std::string& packet)
	{
		std::lock_guard lock{request_buffer_mutex_};
		auto& request_buffer = request_buffers_[socket];
		request_buffer.append(packet);

		while (!request_buffer.empty())
		{
			const auto first = request_buffer.find_first_not_of(" \t\r\n");
			if (first == std::string::npos)
			{
				request_buffer.clear();
				return;
			}

			if (first > 0)
			{
				request_buffer.erase(0, first);
			}

			if (request_buffer.starts_with('{'))
			{
				rapidjson::Document probe{};
				probe.Parse(request_buffer.data(), request_buffer.size());
				if (probe.HasParseError())
				{
					return;
				}

				http_request request{};
				request.body = std::move(request_buffer);
				request_buffer.clear();
				handle_request(request);
				return;
			}

			const auto header_end = request_buffer.find("\r\n\r\n");
			if (header_end == std::string::npos)
			{
				return;
			}

			const auto head = std::string_view{request_buffer}.substr(0, header_end);
			http_request request{};
			if (!parse_request_head(head, request))
			{
				request_buffer.erase(0, header_end + 4);
				continue;
			}

			std::size_t content_length{};
			if (!parse_content_length(request, content_length))
			{
				const auto dispatch = request.method == "GET" || request.method == "HEAD" ||
					request.method == "DELETE";
				request_buffer.erase(0, header_end + 4);
				if (dispatch)
				{
					handle_request(request);
				}

				continue;
			}

			const auto body_start = header_end + 4;
			if (content_length > request_buffer.size() - body_start)
			{
				return;
			}

			request.body = request_buffer.substr(body_start, content_length);
			request_buffer.erase(0, body_start + content_length);
			handle_request(request);
		}
	}

	void http_server::on_disconnect(const SOCKET socket)
	{
		std::lock_guard lock{request_buffer_mutex_};
		request_buffers_.erase(socket);
	}

	void http_server::send_json(const rapidjson::Document& document)
	{
		// The native REST task must still be pending when its start callback returns.
		// A localhost response can otherwise complete synchronously and the game's
		// task manager rejects the already-completed task as a start failure.
		std::this_thread::sleep_for(100ms);

		rapidjson::StringBuffer buffer{};
		rapidjson::Writer<rapidjson::StringBuffer, rapidjson::Document::EncodingType,
			rapidjson::ASCII<>> writer{buffer};
		document.Accept(writer);

		std::string response{};
		response.append("HTTP/1.1 200 OK\r\n");
		response.append("Content-Type: application/json\r\n");
		response.append("X-Signature: 1337\r\n");
		response.append("Connection: close\r\n");
		response.append(utils::string::va("Content-Length: %zu\r\n\r\n", buffer.GetSize()));
		response.append(buffer.GetString(), buffer.GetSize());
		send(response);
		close_connection();
	}
}
