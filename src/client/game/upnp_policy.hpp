#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace upnp::policy
{
	constexpr unsigned lease_seconds = 300;

	inline std::string mapping_key(const std::uint16_t external_port)
	{
		return "<NewRemoteHost></NewRemoteHost>\r\n<NewExternalPort>"
			+ std::to_string(external_port)
			+ "</NewExternalPort>\r\n<NewProtocol>UDP</NewProtocol>\r\n";
	}

	inline std::string soap_body(const std::string_view action, const std::string_view body,
		const std::uint16_t external_port)
	{
		if (action == "DeletePortMapping")
		{
			return mapping_key(external_port);
		}

		std::string result{body};

		if (action == "AddPortMapping")
		{
			constexpr std::string_view old_lease = "<NewLeaseDuration>0</NewLeaseDuration>";

			if (const auto offset = result.find(old_lease); offset != std::string::npos)
			{
				result.replace(offset, old_lease.size(), "<NewLeaseDuration>"
					+ std::to_string(lease_seconds) + "</NewLeaseDuration>");
			}

			constexpr std::string_view old_description = "DemonwarePortMapping";

			if (const auto offset = result.find(old_description); offset != std::string::npos)
			{
				// Keep the body smaller than stock: A99650's SOAP envelope buffer
				// is fixed at 682 bytes, even though the HTTP buffer is larger.
				result.replace(offset, old_description.size(), "S2x Custom Match");
			}
		}

		return result;
	}

	enum class cleanup_action
	{
		pump,
		request_shutdown,
		complete,
		force_cleanup,
	};

	class cleanup
	{
	public:
		using clock = std::chrono::steady_clock;

		explicit cleanup(const clock::time_point deadline)
			: deadline_(deadline)
		{
		}

		cleanup_action next(const unsigned client_state, const unsigned device_state, const clock::time_point now)
		{
			if (!client_state)
			{
				return cleanup_action::complete;
			}

			if (now >= deadline_)
			{
				return cleanup_action::force_cleanup;
			}

			// A99F00: 8 is sending Add, 9 is receiving its response. Aborting
			// here can skip removal of an already installed mapping.
			if (!requested_ && device_state != 8 && device_state != 9)
			{
				requested_ = true;
				return cleanup_action::request_shutdown;
			}

			return cleanup_action::pump;
		}

		void shorten_deadline(const clock::time_point deadline)
		{
			if (deadline < deadline_)
			{
				deadline_ = deadline;
			}
		}

	private:
		clock::time_point deadline_;
		bool requested_{};
	};
}
