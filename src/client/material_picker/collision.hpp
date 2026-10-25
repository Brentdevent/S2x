#pragma once

#include "geometry.hpp"
#include "selection.hpp"

namespace material_picker::collision
{
	void initialize();
	std::optional<picked_material> pick(const geometry::ray& ray);
}
