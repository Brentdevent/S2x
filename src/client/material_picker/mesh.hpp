#pragma once

#include "geometry.hpp"
#include "game/types/rendering.hpp"
#include <vector>

namespace material_picker::mesh_geometry
{
	const game::XModelLodInfo* resident_lod(const game::XModelBase& base);
	std::vector<geometry::triangle> pose_surface(const game::DObj& object, const game::XSurface& surface,
		const game::SkinSurfaceContext& context, const geometry::vector& view_offset);
	bool mark_visible(const game::FxMark& mark, const std::int32_t time);
	std::vector<geometry::triangle> mark_triangles(const game::FxMark& mark, const game::FxMarkVertex* vertices);
}
