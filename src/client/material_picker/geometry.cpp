#include <std_include.hpp>

#include "geometry.hpp"
#include "game/types/rendering.hpp"
#include <algorithm>
#include <cmath>

namespace material_picker::geometry
{
	vector subtract(const vector& a, const vector& b)
	{
		return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
	}

	vector cross(const vector& a, const vector& b)
	{
		return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
	}

	double dot(const vector& a, const vector& b)
	{
		return double(a[0]) * b[0] + double(a[1]) * b[1] + double(a[2]) * b[2];
	}

	bool intersects_bounds(const ray& ray, const float* midpoint, const float* half_size, const float limit)
	{
		double entry = 0, exit_distance = limit;
		for (unsigned int i = 0; i < 3; ++i)
		{
			if (!std::isfinite(midpoint[i]) || !std::isfinite(half_size[i]) || half_size[i] < 0)
			{
				return false;
			}

			const double delta = double(midpoint[i]) - ray.origin[i];
			if (std::abs(ray.direction[i]) < 1.0e-12f)
			{
				if (std::abs(delta) > half_size[i])
				{
					return false;
				}

				continue;
			}

			auto a = (delta - half_size[i]) / ray.direction[i];
			auto b = (delta + half_size[i]) / ray.direction[i];
			if (a > b)
			{
				std::swap(a, b);
			}

			entry = std::max(entry, a);
			exit_distance = std::min(exit_distance, b);
			if (entry > exit_distance)
			{
				return false;
			}
		}

		return true;
	}

	// Two-sided intersection: thin decals and negatively scaled props must
	// remain selectable. Alpha testing and shader displacement are not sampled.
	std::optional<float> intersect_triangle(
		const ray& ray, const vector& a, const vector& b, const vector& c, const float limit)
	{
		const auto e1 = subtract(b, a), e2 = subtract(c, a);
		const auto p = cross(ray.direction, e2);
		const auto determinant = dot(e1, p);
		if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-10)
		{
			return {};
		}

		const auto t = subtract(ray.origin, a);
		const auto u = dot(t, p) / determinant;
		if (u < -1.0e-6 || u > 1.000001)
		{
			return {};
		}

		const auto q = cross(t, e1);
		const auto v = dot(ray.direction, q) / determinant;
		if (v < -1.0e-6 || u + v > 1.000001)
		{
			return {};
		}

		const auto distance = dot(e2, q) / determinant;
		if (!std::isfinite(distance) || distance < 0 || distance > limit)
		{
			return {};
		}

		return static_cast<float>(distance);
	}

	// DObjAnimMat's weight is 2 / dot(quaternion, quaternion), not scale.
	// S2's native mark transform (0x8A6E10) uses these same row vectors.
	bone_transform bone_matrix(const game::DObjAnimMat& bone)
	{
		const auto* q = bone.quaternion;
		const auto w = bone.weight;

		return {
			{{1 - w * (q[1] * q[1] + q[2] * q[2]), w * (q[0] * q[1] + q[2] * q[3]), w * (q[0] * q[2] - q[1] * q[3])},
				{w * (q[0] * q[1] - q[2] * q[3]), 1 - w * (q[0] * q[0] + q[2] * q[2]), w * (q[1] * q[2] + q[0] * q[3])},
				{w * (q[0] * q[2] + q[1] * q[3]), w * (q[1] * q[2] - q[0] * q[3]),
					1 - w * (q[0] * q[0] + q[1] * q[1])}},
			{bone.translation[0], bone.translation[1], bone.translation[2]}};
	}

	vector pose_vertex(
		const vector& vertex, const bone_transform& bind, const bone_transform& pose, const vector& view_offset)
	{
		const auto delta = subtract(vertex, bind.origin);
		const vector local{static_cast<float>(dot(delta, bind.axis[0])), static_cast<float>(dot(delta, bind.axis[1])),
			static_cast<float>(dot(delta, bind.axis[2]))};
		vector result{};
		for (unsigned int i = 0; i < 3; ++i)
		{
			result[i] = pose.origin[i] + view_offset[i] + local[0] * pose.axis[0][i] + local[1] * pose.axis[1][i]
				+ local[2] * pose.axis[2][i];
		}

		return result;
	}

	// Axis rows are local axes in world space (native 0x75C0C0/0x8A9E80).
	// Do not normalize the transformed direction: t stays in world units even
	// for nonuniform or negative scale.
	std::optional<ray> to_model(const ray& world, const float* origin, const float (&axis)[3][3], const float* scale)
	{
		const vector delta{world.origin[0] - origin[0], world.origin[1] - origin[1], world.origin[2] - origin[2]};
		ray local{};
		for (unsigned int i = 0; i < 3; ++i)
		{
			if (!std::isfinite(scale[i]) || std::abs(scale[i]) < 1.0e-6f)
			{
				return {};
			}

			const vector row{axis[i][0], axis[i][1], axis[i][2]};
			local.origin[i] = static_cast<float>(dot(delta, row) / scale[i]);
			local.direction[i] = static_cast<float>(dot(world.direction, row) / scale[i]);
			if (!std::isfinite(local.origin[i]) || !std::isfinite(local.direction[i]))
			{
				return {};
			}
		}

		return local;
	}
}
