#pragma once

#include "achievement_store.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace demonware::achievement_response
{
	rapidjson::Value serialize_achievements(
		const std::vector<achievement_record>& achievements,
		rapidjson::Document::AllocatorType& allocator);

	std::string make_get_user_achievements_response(std::string_view client_transaction);
}
