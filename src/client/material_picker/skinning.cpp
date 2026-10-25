#include <std_include.hpp>

#include "skinning.hpp"
#include <cmath>

namespace material_picker::skinning
{
	namespace
	{
		using quaternion = std::array<float, 4>;
		struct dual_quaternion
		{
			quaternion real;
			quaternion dual;
		};

		quaternion multiply(const quaternion& a, const quaternion& b)
		{
			return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
				a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
				a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
				a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
		}

		quaternion conjugate(const quaternion& q)
		{
			return {-q[0], -q[1], -q[2], q[3]};
		}

		float dot(const quaternion& a, const quaternion& b)
		{
			return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
		}

		bool normalize(quaternion& q)
		{
			const auto length_squared = dot(q, q);
			if (!std::isfinite(length_squared) || length_squared < 1.0e-12f)
			{
				return false;
			}

			const auto inverse_length = 1.0f / std::sqrt(length_squared);
			for (auto& x : q)
			{
				x *= inverse_length;
			}

			return true;
		}

		struct bone
		{
			geometry::bone_transform bind;
			geometry::bone_transform pose;
			dual_quaternion transform;
			bool valid{};
		};

		bone make_bone(const game::DObjAnimMat& bind, const game::DObjAnimMat& pose)
		{
			bone result{};
			result.bind = geometry::bone_matrix(bind);
			result.pose = geometry::bone_matrix(pose);
			quaternion a{bind.quaternion[0], bind.quaternion[1], bind.quaternion[2], bind.quaternion[3]};
			quaternion b{pose.quaternion[0], pose.quaternion[1], pose.quaternion[2], pose.quaternion[3]};
			if (!normalize(a) || !normalize(b))
			{
				return result;
			}

			// 0x8B06B0: pose * inverse(bind), with dual = .5 * translation * real.
			result.transform.real = multiply(b, conjugate(a));
			const auto t = geometry::pose_vertex({}, result.bind, result.pose, {});
			for (const auto x : t)
			{
				if (!std::isfinite(x))
				{
					return result;
				}
			}

			result.transform.dual = multiply({t[0], t[1], t[2], 0}, result.transform.real);
			for (auto& x : result.transform.dual)
			{
				x *= 0.5f;
			}

			result.valid = true;

			return result;
		}

		std::optional<geometry::vector> pose_vertex(const geometry::vector& position,
			const game::XSurfaceVertexWeights& packed, const std::vector<bone>& bones, const bool linear)
		{
			if (packed.count < 1 || packed.count > 4)
			{
				return {};
			}

			std::array<float, 4> weights{1, 0, 0, 0};
			for (unsigned int i = 1; i < packed.count; ++i)
			{
				// 0x8B1BD0 / 0x8B2000 use 1/65536, not UNORM's 1/65535.
				weights[i] = float(packed.weights[i - 1]) / 65536.0f;
				weights[0] -= weights[i];
			}

			if (weights[0] < 0)
			{
				return {};
			}

			for (unsigned int i = 0; i < packed.count; ++i)
			{
				if (packed.bones[i] >= bones.size() || !bones[packed.bones[i]].valid)
				{
					return {};
				}
			}

			geometry::vector result{};
			if (linear || packed.count == 1)
			{
				for (unsigned int i = 0; i < packed.count; ++i)
				{
					const auto& b = bones[packed.bones[i]];
					const auto p = geometry::pose_vertex(position, b.bind, b.pose, {});
					for (unsigned int j = 0; j < 3; ++j)
					{
						result[j] += weights[i] * p[j];
					}
				}
			}
			else
			{
				dual_quaternion blend{};
				const auto& reference = bones[packed.bones[0]].transform.real;
				for (unsigned int i = 0; i < packed.count; ++i)
				{
					const auto& dq = bones[packed.bones[i]].transform;

					// Native 0x8B2000 fixes antipodal signs against the first bone.
					const auto weight = std::signbit(dot(reference, dq.real)) ? -weights[i] : weights[i];
					for (unsigned int j = 0; j < 4; ++j)
					{
						blend.real[j] += weight * dq.real[j];
						blend.dual[j] += weight * dq.dual[j];
					}
				}

				const auto length_squared = dot(blend.real, blend.real);
				if (!std::isfinite(length_squared) || length_squared < 1.0e-12f)
				{
					return {};
				}

				const auto inverse_length = 1.0f / std::sqrt(length_squared);
				for (unsigned int j = 0; j < 4; ++j)
				{
					blend.real[j] *= inverse_length;
					blend.dual[j] *= inverse_length;
				}

				const auto inverse = conjugate(blend.real);
				const auto rotated =
					multiply(multiply(blend.real, {position[0], position[1], position[2], 0}), inverse);
				const auto translation = multiply(blend.dual, inverse);
				for (unsigned int j = 0; j < 3; ++j)
				{
					result[j] = rotated[j] + 2 * translation[j];
				}
			}

			for (const auto x : result)
			{
				if (!std::isfinite(x))
				{
					return {};
				}
			}

			return result;
		}
	}

	std::vector<geometry::triangle> pose_surface(const game::DObj& object, const game::XSurface& surface,
		const game::SkinSurfaceContext& context, const geometry::vector& view_offset, const bool linear)
	{
		std::vector<geometry::triangle> result;
		const auto* stream = surface.stream;

		// Blend-shape positions and shader-only deformation need their own path;
		// never substitute the undeformed mesh when a morph target is present.
		if (!(surface.flags & 4) || !stream || stream->blend_shape_count || !stream->loaded
			|| !stream->loaded->positions || !stream->loaded->weights || !context.base_pose || !object.bones
			|| !context.bone_count)
		{
			return result;
		}

		const auto* indices = surface.indices ? surface.indices : stream->loaded->indices;
		if (!indices)
		{
			return result;
		}

		const auto total_bones = unsigned(object.bone_count) + object.extra_bone_count;
		if (total_bones > std::size(object.hidden) * 32 || context.bone_offset > total_bones
			|| context.bone_count > total_bones - context.bone_offset)
		{
			return result;
		}

		std::vector<bone> bones(context.bone_count);
		for (unsigned int i = 0; i < context.bone_count; ++i)
		{
			const auto global = context.bone_offset + i;
			if (object.hidden[global >> 5] & (0x80000000u >> (global & 31)))
			{
				continue;
			}

			bones[i] = make_bone(context.base_pose[i], object.bones[global]);
		}

		std::vector<std::optional<geometry::vector>> positions(surface.vertex_count);
		for (unsigned int i = 0; i < surface.vertex_count; ++i)
		{
			const auto* xyz = stream->loaded->positions[i].xyz;
			positions[i] = pose_vertex({xyz[0], xyz[1], xyz[2]}, stream->loaded->weights[i], bones, linear);
			if (!positions[i])
			{
				continue;
			}

			for (unsigned int j = 0; j < 3; ++j)
			{
				(*positions[i])[j] += view_offset[j];
			}
		}

		for (unsigned int t = 0; t < surface.triangle_count; ++t)
		{
			const auto* tri = indices + 3 * t;
			if (tri[0] >= positions.size() || tri[1] >= positions.size() || tri[2] >= positions.size()
				|| !positions[tri[0]] || !positions[tri[1]] || !positions[tri[2]])
			{
				continue;
			}

			result.push_back({*positions[tri[0]], *positions[tri[1]], *positions[tri[2]]});
		}

		return result;
	}
}
