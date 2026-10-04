#pragma once
#include <xsk/gsc/engine/s2.hpp>

namespace gsc
{
	extern std::unique_ptr<xsk::gsc::s2::context> gsc_ctx;

	game::ScriptFile* find_script(game::XAssetType type, const char* name, int allow_create_default);

	void on_before_main(const std::function<void()>& callback);
}
