#pragma once

#include "game/types/materials.hpp"
#include <exception>
#include <string>
#include <vector>

namespace material_picker
{
	inline constexpr float trace_distance = 8192.0f;
	struct render_material
	{
		std::string name;
		std::string techset;
		std::vector<std::string> images;
		std::string source;
		float distance{};
	};

	struct picked_material
	{
		std::string name;
		float distance{};
		std::uint32_t surface_flags{};
		std::uint64_t contents{};
		std::vector<render_material> matches;
		std::exception_ptr error;
		bool render_geometry{};
	};

	render_material copy_material(const game::Material& material);
	void resolve_material(picked_material& picked);
}
