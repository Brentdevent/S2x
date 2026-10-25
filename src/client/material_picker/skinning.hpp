#pragma once

#include "geometry.hpp"
#include "game/types/rendering.hpp"
#include <vector>

namespace material_picker::skinning
{
	std::vector<geometry::triangle> pose_surface(const game::DObj& object, const game::XSurface& surface,
		const game::SkinSurfaceContext& context, const geometry::vector& view_offset, const bool linear);
}
