#pragma once

#include <rapidjson/document.h>

namespace demonware::identity_response
{
	// Returns a null document until the main-thread owned identity is ready.
	// Callers must not send a successful zero-ID/token response in that state.
	rapidjson::Document make_umbrella_lsg_token();
	rapidjson::Document make_uno_identity_token();
}
