#include <std_include.hpp>
#include "content_server.hpp"
#include "../content_store.hpp"
#include "component/console/console.hpp"

#include <charconv>

namespace demonware
{
	void content_server::handle_request(const http_request& request)
	{
		std::string status = "404 Not Found";
		std::string body;
		try
		{
			if (request.method == "PUT" && request.target.starts_with("/uploads/"))
			{
				status = content_store::stage_upload(request.target.substr(9), request.body)
					? "200 OK" : "400 Bad Request";
			}
			else if ((request.method == "GET" || request.method == "HEAD") && request.target.starts_with("/files/"))
			{
				std::uint64_t id{};
				const auto* first = request.target.data() + 7;
				const auto* last = request.target.data() + request.target.size();
				const auto parsed = std::from_chars(first, last, id);
				if (parsed.ec == std::errc{} && parsed.ptr == last)
				{
					if (auto record = content_store::find_file(id))
					{
						status = "200 OK";
						body = std::move(record->data);
					}
				}
			}
		}
		catch (const std::exception& error)
		{
			console::error("[DW]: Paint job transfer failed: %s\n", error.what());
			status = "500 Internal Server Error";
		}
		auto response = "HTTP/1.1 " + status + "\r\nContent-Type: application/octet-stream\r\nContent-Length: "
			+ std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
		if (request.method != "HEAD")
		{
			response += body;
		}
		send(response);
		close_connection();
	}

	size_t content_server::maximum_request_size() const
	{
		return 32768;
	}
}
