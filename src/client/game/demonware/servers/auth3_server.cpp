#include <std_include.hpp>
#include "../dw_include.hpp"

#include "auth3_server.hpp"

#include "component/console/console.hpp"

#include <utils/cryptography.hpp>
#include <utils/string.hpp>
#include <utils/flags.hpp>

#include <charconv>

namespace demonware
{
	namespace
	{
		constexpr std::array<char, 32> steam_auth_magic{'S', '2', 'x'};

#pragma pack(push, 1)
		struct steam_auth_token
		{
			char m_magic[32];
			char m_authKey[24];
			unsigned __int64 m_userID;
			char m_username[64];
		};

		struct auth_ticket
		{
			unsigned int m_magicNumber;
			char m_type;
			unsigned int m_titleID;
			unsigned int m_timeIssued;
			unsigned int m_timeExpires;
			unsigned __int64 m_licenseID;
			unsigned __int64 m_userID;
			char m_username[64];
			char m_sessionKey[24];
			char m_usingHashMagicNumber[3];
			char m_hash[4];
		};
#pragma pack(pop)

		static_assert(sizeof(steam_auth_token) == 128);
		static_assert(sizeof(auth_ticket) == 128);

		bool parse_unsigned_integer(const rapidjson::Value& value, unsigned int& result)
		{
			if (!value.IsString())
			{
				return false;
			}

			const auto* begin = value.GetString();
			const auto* end = begin + value.GetStringLength();
			const auto [position, error] = std::from_chars(begin, end, result);
			return error == std::errc{} && position == end;
		}
	}

	void auth3_server::send_reply(reply* data)
	{
		if (!data) return;
		this->send(data->data());
	}

	void auth3_server::handle(const std::string& packet)
	{
		if (packet.starts_with("POST /auth/"))
		{
			console::demonware("[DW]: [auth]: user requested authentication.\n");
			return;
		}

		unsigned int title_id = 0;
		unsigned int iv_seed = 0;
		std::string identity{};
		std::string token{};

		rapidjson::Document j;
		j.Parse(packet.data(), packet.size());
		if (j.HasParseError() || !j.IsObject())
		{
			console::error("[DW]: [auth]: received an invalid authentication request.\n");
			return;
		}

		if (!j.HasMember("title_id") || !parse_unsigned_integer(j["title_id"], title_id)
			|| !j.HasMember("iv_seed") || !parse_unsigned_integer(j["iv_seed"], iv_seed))
		{
			console::error("[DW]: [auth]: received invalid authentication parameters.\n");
			return;
		}

		if (j.HasMember("identity") && j["identity"].IsString())
		{
			identity = j["identity"].GetString();
		}

		if (j.HasMember("extra_data") && j["extra_data"].IsString())
		{
			rapidjson::Document extra_data;
			auto& ed = j["extra_data"];
			extra_data.Parse(ed.GetString(), ed.GetStringLength());

			if (!extra_data.HasParseError() && extra_data.IsObject()
				&& extra_data.HasMember("token") && extra_data["token"].IsString())
			{
				auto& token_field = extra_data["token"];
				std::string token_b64(token_field.GetString(), token_field.GetStringLength());
				token = utils::cryptography::base64::decode(token_b64);
			}
		}

		if (token.size() != sizeof(steam_auth_token))
		{
			console::error("[DW]: [auth]: received an invalid authentication token.\n");
			return;
		}

		steam_auth_token steam_token{};
		std::memcpy(&steam_token, token.data(), sizeof(steam_token));
		if (!std::equal(steam_auth_magic.begin(), steam_auth_magic.end(), steam_token.m_magic)
			|| steam_token.m_userID == 0)
		{
			console::error("[DW]: [auth]: received an invalid authentication token.\n");
			return;
		}

		console::demonware("[DW]: [auth]: authenticating user %.*s\n",
			static_cast<int>(sizeof(steam_token.m_username)), steam_token.m_username);

		std::string auth_key(steam_token.m_authKey, sizeof(steam_token.m_authKey));
		std::string session_key(
			"\x13\x37\x13\x37\x13\x37\x13\x37\x13\x37\x13\x37\x13\x37\x13\x37\x13\x37\x13\x37\x13\x37\x13\x37", 24);

		// client_ticket
		auth_ticket ticket{};
		std::memset(&ticket, 0x0, sizeof ticket);
		ticket.m_magicNumber = 0x0EFBDADDE;
		ticket.m_type = 0;
		ticket.m_titleID = title_id;
		ticket.m_timeIssued = static_cast<uint32_t>(time(nullptr));
		ticket.m_timeExpires = ticket.m_timeIssued + 30000;
		ticket.m_licenseID = 0;
		ticket.m_userID = steam_token.m_userID;
		std::memcpy(ticket.m_username, steam_token.m_username, sizeof(ticket.m_username));
		std::memcpy(ticket.m_sessionKey, session_key.data(), 24);

		const auto iv = utils::cryptography::tiger::compute(std::string(reinterpret_cast<char*>(&iv_seed), 4));
		const auto ticket_enc = utils::cryptography::des3::encrypt(
			std::string(reinterpret_cast<char*>(&ticket), sizeof(ticket)), iv, auth_key);
		const auto ticket_b64 = utils::cryptography::base64::encode(
			reinterpret_cast<const unsigned char*>(ticket_enc.data()), 128);

		// server_ticket
		uint8_t auth_data[128];
		std::memset(&auth_data, 0, sizeof auth_data);
		std::memcpy(auth_data, session_key.data(), 24);
		const auto auth_data_b64 = utils::cryptography::base64::encode(auth_data, 128);

		demonware::set_session_key(session_key);

		// header time
		char date[64];
		const auto now = time(nullptr);
		tm gmtm{};
		gmtime_s(&gmtm, &now);
		strftime(date, 64, "%a, %d %b %G %T", &gmtm);

		// json content
		rapidjson::Document doc;
		doc.SetObject();

		doc.AddMember("auth_task", "29", doc.GetAllocator());
		doc.AddMember("code", "700", doc.GetAllocator());

		auto seed = std::to_string(iv_seed);
		doc.AddMember("iv_seed", rapidjson::StringRef(seed.data(), seed.size()), doc.GetAllocator());
		doc.AddMember("client_ticket", rapidjson::StringRef(ticket_b64.data(), ticket_b64.size()), doc.GetAllocator());
		doc.AddMember("server_ticket", rapidjson::StringRef(auth_data_b64.data(), auth_data_b64.size()),
		              doc.GetAllocator());
		doc.AddMember("client_id", "", doc.GetAllocator());
		doc.AddMember("account_type", "steam", doc.GetAllocator());
		doc.AddMember("crossplay_enabled", false, doc.GetAllocator());
		doc.AddMember("loginqueue_enabled", false, doc.GetAllocator());

		rapidjson::Value value{};
		doc.AddMember("lsg_endpoint", value, doc.GetAllocator());

		std::string extended_data = ""; // maybe figure out what this is supposed to be
		std::string extra_data = utils::string::va("{\"extended_data\": \"%s\"}", extended_data.data());
		// extra data
		doc.AddMember("extra_data", rapidjson::StringRef(extra_data.data(), extra_data.size()), doc.GetAllocator());

		//doc.AddMember("identity", rapidjson::StringRef(identity.data(), identity.size()), doc.GetAllocator());

		rapidjson::StringBuffer buffer{};
		rapidjson::Writer<rapidjson::StringBuffer, rapidjson::Document::EncodingType, rapidjson::ASCII<>>
			writer(buffer);
		doc.Accept(writer);

		std::string x_signature = "1337"; // maybe figure out how to compute this (patched in demonware.cpp)

		// http stuff
		std::string result;
		result.append("HTTP/1.1 200 OK\r\n");
		result.append("Server: TornadoServer/6.0.3\r\n");
		result.append("Content-Type: application/json\r\n");
		result.append(utils::string::va("Date: %s GMT\r\n", date));
		result.append(utils::string::va("X-Signature: %s\r\n", x_signature.data()));
		result.append(utils::string::va("Content-Length: %d\r\n\r\n", buffer.GetLength()));
		result.append(buffer.GetString(), buffer.GetLength());

		raw_reply reply(result);

		this->send_reply(&reply);

		console::demonware("[DW]: [auth]: user successfully authenticated.\n");
	}
}
