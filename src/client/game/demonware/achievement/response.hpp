#pragma once

#include "store.hpp"

namespace demonware::achievement_response
{
	constexpr std::string_view get_user_achievements_action = "get_user_achievements";
	constexpr std::string_view get_scheduled_user_achievements_action = "get_scheduled_user_achievements";
	constexpr std::string_view get_user_achievements_for_users_action = "get_user_achievements_for_users";

	constexpr std::size_t maximum_request_length = 6143;
	constexpr std::size_t maximum_client_transaction_length = 24;
	constexpr std::size_t maximum_page_token_length = 31;
	constexpr std::uint32_t maximum_page_size = 50;
	constexpr std::size_t maximum_response_length = 64 * 1024;

	struct user_achievements_request
	{
		std::string client_transaction{};
		std::vector<achievement_status> statuses{};
		std::vector<int> kinds{};
		std::vector<std::string> names{};
		std::size_t offset{};
		std::uint32_t limit{};
	};

	rapidjson::Value serialize_achievements(const std::vector<achievement_record>& achievements,
		rapidjson::Document::AllocatorType& allocator);

	bool parse_get_user_achievements_request(std::string_view json, user_achievements_request& request);
	bool parse_get_user_achievements_for_users_request(std::string_view json, user_achievements_request& request,
		std::uint64_t& user_id);
	bool parse_get_scheduled_user_achievements_request(std::string_view json, std::string& client_transaction);

	std::optional<std::string> make_get_user_achievements_response(const user_achievements_request& request,
		const std::vector<achievement_record>& achievements);
	std::optional<std::string> make_get_user_achievements_for_users_response(const user_achievements_request& request,
		std::uint64_t user_id, const std::vector<achievement_record>& achievements);
	std::optional<std::string> make_empty_scheduled_user_achievements_response(std::string_view client_transaction);
}
