#pragma once

#include "dynamic.hpp"
#include "selection.hpp"

namespace material_picker::scene
{
	std::optional<picked_material> pick(const geometry::ray& ray, const dynamic_geometry::snapshot& dynamic);
}
