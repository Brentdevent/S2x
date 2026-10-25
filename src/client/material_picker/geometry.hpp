#pragma once

#include <array>
#include <optional>

namespace game
{
	struct DObjAnimMat;
}

namespace material_picker::geometry
{
	using vector = std::array<float, 3>;
	struct ray
	{
		vector origin;
		vector direction;
	};

	struct triangle
	{
		vector a;
		vector b;
		vector c;
	};

	struct bone_transform
	{
		vector axis[3];
		vector origin;
	};

	vector subtract(const vector& a, const vector& b);
	vector cross(const vector& a, const vector& b);
	double dot(const vector& a, const vector& b);
	bool intersects_bounds(const ray& ray, const float* midpoint, const float* half_size, const float limit);
	std::optional<float> intersect_triangle(
		const ray& ray, const vector& a, const vector& b, const vector& c, const float limit);
	bone_transform bone_matrix(const game::DObjAnimMat& bone);
	vector pose_vertex(
		const vector& vertex, const bone_transform& bind, const bone_transform& pose, const vector& view_offset);
	std::optional<ray> to_model(const ray& world, const float* origin, const float (&axis)[3][3], const float* scale);
}
