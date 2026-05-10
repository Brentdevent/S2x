#pragma once

#include "game.hpp"

namespace dvars
{
	extern game::dvar_t* con_inputBoxColor;
	extern game::dvar_t* con_inputHintBoxColor;
	extern game::dvar_t* con_outputBarColor;
	extern game::dvar_t* con_outputSliderColor;
	extern game::dvar_t* con_outputWindowColor;
	extern game::dvar_t* con_inputDvarMatchColor;
	extern game::dvar_t* con_inputDvarValueColor;
	extern game::dvar_t* con_inputDvarInactiveValueColor;
	extern game::dvar_t* con_inputCmdMatchColor;

	std::string dvar_get_domain(const game::DvarType type, const game::DvarLimits& domain);
	std::string dvar_get_vector_domain(const int components, const game::DvarLimits& domain);
}
