#pragma once

#include "capture.hpp"
#include <string>

namespace material_picker::dynamic_geometry
{
	struct mesh
	{
		std::string name;
		std::string techset;
		std::vector<std::string> images;
		std::string source;
		std::vector<geometry::triangle> triangles;
	};

	using snapshot = frame_capture<mesh>::snapshot;

	void install();
	void clear();
	snapshot advance(const std::optional<geometry::ray>& next);
}
