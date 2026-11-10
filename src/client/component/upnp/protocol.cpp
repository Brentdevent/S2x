#include <std_include.hpp>
#include "protocol.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <optional>
#include <shlwapi.h>
#include <xmllite.h>

#pragma comment(lib, "xmllite.lib")

namespace upnp::protocol
{
	namespace
	{
		std::string_view trim_http_space(const std::string_view value)
		{
			const auto begin = value.find_first_not_of(" \t");
			return begin == std::string_view::npos ? std::string_view{}
				: value.substr(begin, value.find_last_not_of(" \t") - begin + 1);
		}

		bool read_http_line(std::string_view& input, std::string_view& line)
		{
			const auto end = input.find("\r\n");

			if (end == std::string_view::npos)
			{
				return false;
			}

			line = input.substr(0, end);
			input.remove_prefix(end + 2);
			return std::ranges::none_of(line, [](const unsigned char character)
			{
				return (character < 32 && character != '\t') || character == 127;
			});
		}

		bool read_http_field(std::string_view& input, const std::string_view line, std::string& name, std::string& value)
		{
			const auto separator = line.find(':');
			const auto field_name = line.substr(0, separator);

			if (separator == std::string_view::npos || field_name.empty()
				|| field_name.find_first_not_of("!#$%&'*+-.^_`|~0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")
					!= std::string_view::npos)
			{
				return false;
			}

			name.assign(field_name);
			std::transform(name.begin(), name.end(), name.begin(), [](const unsigned char character)
			{
				return static_cast<char>(std::tolower(character));
			});

			value.assign(line.substr(separator + 1));

			// Legacy routers can fold field values across physical lines.
			// Consume continuations here so they cannot become separate headers.
			while (input.starts_with(' ') || input.starts_with('\t'))
			{
				std::string_view continuation;

				if (!read_http_line(input, continuation))
				{
					return false;
				}

				value.push_back(' ');
				value.append(trim_http_space(continuation));
			}

			return true;
		}

		bool read_chunked_body(std::string_view input, std::string& body)
		{
			std::string_view line;

			while (read_http_line(input, line))
			{
				// Extensions are metadata, not part of the size or XML payload.
				const auto size_text = trim_http_space(line.substr(0, line.find(';')));

				if (size_text.empty())
				{
					return false;
				}

				std::size_t size{};
				const auto parsed = std::from_chars(size_text.data(), size_text.data() + size_text.size(), size, 16);

				if (parsed.ec != std::errc{} || parsed.ptr != size_text.data() + size_text.size())
				{
					return false;
				}

				if (!size)
				{
					// Require the complete trailer section, but never treat it as XML.
					while (read_http_line(input, line))
					{
						if (line.empty())
						{
							return input.empty();
						}

						std::string name;
						std::string value;

						if (!read_http_field(input, line, name, value) || name == "content-length" || name == "transfer-encoding")
						{
							return false;
						}
					}

					return false;
				}

				if (size > input.size() || !input.substr(size).starts_with("\r\n"))
				{
					return false;
				}

				// Every appended byte comes from the bounded native response buffer.
				body.append(input.substr(0, size));
				input.remove_prefix(size + 2);
			}

			return false;
		}
	}

	bool read_http_body(std::string_view input, std::string& body, const bool allow_fault)
	{
		std::string_view line;

		if (!read_http_line(input, line)
			|| (!line.starts_with("HTTP/1.1 200 ") && !line.starts_with("HTTP/1.0 200 ")
				&& !(allow_fault && (line.starts_with("HTTP/1.1 500 ") || line.starts_with("HTTP/1.0 500 ")))))
		{
			return false;
		}

		const auto http11 = line.starts_with("HTTP/1.1 ");
		std::optional<std::size_t> length;
		bool transfer_encoding{}, chunked{};
		body.clear();

		while (read_http_line(input, line))
		{
			if (line.empty())
			{
				if (transfer_encoding)
				{
					return chunked && read_chunked_body(input, body);
				}

				if (length && *length != input.size())
				{
					return false;
				}

				body.assign(input);
				return true;
			}

			std::string name;
			std::string field;

			if (!read_http_field(input, line, name, field))
			{
				return false;
			}

			auto value = trim_http_space(field);

			if (name == "transfer-encoding")
			{
				if (!http11 || length)
				{
					return false;
				}

				transfer_encoding = true;

				// Field lines form one list. Empty items carry no coding.
				while (!value.empty())
				{
					const auto separator = value.find(',');
					auto coding = trim_http_space(value.substr(0, separator));
					value = separator == std::string_view::npos ? std::string_view{} : value.substr(separator + 1);

					if (coding.empty())
					{
						continue;
					}

					// UPnP examples also use a quoted "chunked" token.
					if (coding.size() >= 2 && coding.front() == '"' && coding.back() == '"')
					{
						coding = coding.substr(1, coding.size() - 2);
					}

					const auto supported = std::ranges::equal(coding, "chunked"sv, [](const unsigned char left, const char right)
					{
						return std::tolower(left) == right;
					});

					if (chunked || !supported)
					{
						return false;
					}

					chunked = true;
				}
			}
			else if (name == "content-length")
			{
				if (length || transfer_encoding || value.empty())
				{
					return false;
				}

				std::size_t size{};
				const auto parsed = std::from_chars(value.data(), value.data() + value.size(), size);

				if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
				{
					return false;
				}

				length = size;
			}
		}

		return false;
	}

	bool read_mapping_response(const std::string_view body, std::array<std::string, 3>& fields)
	{
		CComPtr<IStream> stream;
		stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(body.data()), static_cast<UINT>(body.size())));
		CComPtr<IXmlReader> reader;

		if (!stream || FAILED(CreateXmlReader(IID_PPV_ARGS(&reader), nullptr))
			|| FAILED(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit))
			|| FAILED(reader->SetProperty(XmlReaderProperty_ConformanceLevel, XmlConformanceLevel_Document))
			|| FAILED(reader->SetInput(stream)))
		{
			return false;
		}

		constexpr std::wstring_view soap_namespace = L"http://schemas.xmlsoap.org/soap/envelope/";
		constexpr std::array<std::wstring_view, 3> names{
			L"NewInternalPort", L"NewInternalClient", L"NewPortMappingDescription"};
		std::array<bool, 3> seen{};
		std::string* field{};
		bool in_body{}, in_response{}, saw_body{}, saw_response{};
		XmlNodeType type{};
		HRESULT result{};

		while ((result = reader->Read(&type)) == S_OK)
		{
			UINT depth{};

			if (FAILED(reader->GetDepth(&depth)))
			{
				return false;
			}

			if (type == XmlNodeType_Element)
			{
				const wchar_t* local_name{};
				const wchar_t* namespace_uri{};

				if (field || FAILED(reader->GetLocalName(&local_name, nullptr))
					|| FAILED(reader->GetNamespaceUri(&namespace_uri, nullptr)))
				{
					return false;
				}

				const std::wstring_view name{local_name};
				const std::wstring_view ns{namespace_uri};

				if (depth == 0 && (name != L"Envelope" || ns != soap_namespace))
				{
					return false;
				}

				if (depth == 1 && name == L"Body" && ns == soap_namespace)
				{
					if (saw_body || reader->IsEmptyElement())
					{
						return false;
					}

					in_body = saw_body = true;
				}
				else if (depth == 2 && in_body)
				{
					if (saw_response || reader->IsEmptyElement() || name != L"GetSpecificPortMappingEntryResponse"
						|| (ns != L"urn:schemas-upnp-org:service:WANIPConnection:1"
							&& ns != L"urn:schemas-upnp-org:service:WANPPPConnection:1"))
					{
						return false;
					}

					in_response = saw_response = true;
				}
				else if (depth == 3 && in_response && ns.empty())
				{
					// Only direct, unqualified result arguments establish ownership.
					for (std::size_t i = 0; i < names.size(); ++i)
					{
						if (name != names[i])
						{
							continue;
						}

						if (seen[i] || reader->IsEmptyElement())
						{
							return false;
						}

						seen[i] = true;
						field = &fields[i];
					}
				}
			}
			else if (type == XmlNodeType_EndElement)
			{
				// XmlLite's closing-node depth includes the element being closed.
				if (depth == 2)
				{
					in_body = false;
				}

				if (depth == 3)
				{
					in_response = false;
				}

				if (depth == 4)
				{
					field = nullptr;
				}
			}
			else if (field && (type == XmlNodeType_Text || type == XmlNodeType_CDATA || type == XmlNodeType_Whitespace))
			{
				const wchar_t* text{};
				UINT size{};

				if (FAILED(reader->GetValue(&text, &size)))
				{
					return false;
				}

				for (const auto character : std::wstring_view{text, size})
				{
					if (character > 127)
					{
						return false;
					}

					field->push_back(static_cast<char>(character));
				}
			}
		}

		// Read through EOF: a matching prefix in a truncated document is insufficient.
		if (result != S_FALSE || !saw_response)
		{
			return false;
		}

		for (auto& value : fields)
		{
			const auto begin = value.find_first_not_of(" \t\r\n");

			if (begin == std::string::npos)
			{
				return false;
			}

			value = value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
		}

		return true;
	}
}
