#pragma once

#include <rapidjson/document.h>

namespace demonware::identity_response
{
	rapidjson::Document make_umbrella_lsg_token();
	rapidjson::Document make_uno_identity_token();
}
