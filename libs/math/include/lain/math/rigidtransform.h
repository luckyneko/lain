#pragma once

#include "lain/math/axisconvention.h"
#include "lain/math/types.h"

#include <cassert>
#include <optional>

namespace lain::math
{
	// A rotation followed by a translation: the transform that maps points between two named
	// coordinate frames with no scale, shear or projective part (CONTEXT.md, "Rigid transform").
	//
	// It does not know which frames it relates; the name of the field holding it does. Name fields
	// aFromB, meaning "maps B-frame coordinates into frame A", and composition reads left to right
	// the way the frames chain: aFromC = aFromB * bFromC.
	//
	// The rotation is a unit quaternion, re-normalised on construction and composition, so drift
	// from long chains of products cannot accumulate into a scale.
	template <typename T>
	class RigidTransform
	{
	public:
		// The identity.
		RigidTransform() = default;

		// PRECONDITION: `rotation` is not the zero quaternion (asserted in debug). Normalised here.
		RigidTransform(const Quat<T>& rotation, const Vec3<T>& translation)
			: m_rotation(normalize(rotation))
			, m_translation(translation)
		{
			assert(dot(rotation, rotation) > T(0) && "a rotation cannot be the zero quaternion");
		}

		// A 4x4 matrix as a rigid transform, or nullopt when it is not one within `tolerance`: its
		// 3x3 part must be orthonormal with determinant +1 (no scale, shear or reflection) and its
		// bottom row must be (0, 0, 0, 1) (nothing projective). A general matrix is data, so this
		// refuses rather than quietly extracting the nearest rotation.
		static std::optional<RigidTransform> fromMatrix(const Mat<4, 4, T>& matrix, T tolerance = T(1e-6))
		{
			// GLM is column-major: matrix[column][row], so the bottom row is matrix[c][3].
			for (length_t c = 0; c < 4; ++c)
			{
				if (abs(matrix[c][3] - (c == 3 ? T(1) : T(0))) > tolerance)
					return std::nullopt;
			}
			const Mat<3, 3, T> rotation{matrix};
			const Mat<3, 3, T> gram = transpose(rotation) * rotation;
			for (length_t c = 0; c < 3; ++c)
			{
				for (length_t r = 0; r < 3; ++r)
				{
					if (abs(gram[c][r] - (c == r ? T(1) : T(0))) > tolerance)
						return std::nullopt;
				}
			}
			if (determinant(rotation) <= T(0))
				return std::nullopt;
			return RigidTransform{quat_cast(rotation), Vec3<T>{matrix[3]}};
		}

		const Quat<T>& rotation() const { return m_rotation; }
		const Vec3<T>& translation() const { return m_translation; }

		// A point mapped into the target frame: rotated, then translated.
		Vec3<T> apply(const Vec3<T>& point) const { return m_rotation * point + m_translation; }
		// A direction mapped into the target frame: rotated only, since a direction has no position.
		Vec3<T> rotate(const Vec3<T>& direction) const { return m_rotation * direction; }

		// (a * b).apply(p) == a.apply(b.apply(p)).
		RigidTransform operator*(const RigidTransform& rhs) const
		{
			return RigidTransform{m_rotation * rhs.m_rotation, m_rotation * rhs.m_translation + m_translation};
		}

		// bFromA for this aFromB.
		RigidTransform inverse() const
		{
			const Quat<T> inverted = conjugate(m_rotation); // a unit quaternion's inverse
			return RigidTransform{inverted, -(inverted * m_translation)};
		}

		// The homogeneous 4x4 form, for code that speaks matrices.
		Mat<4, 4, T> matrix() const
		{
			Mat<4, 4, T> out = mat4_cast(m_rotation);
			out[3] = Vec4<T>{m_translation, T(1)};
			return out;
		}

	private:
		Quat<T> m_rotation{T(1), T(0), T(0), T(0)}; // w, x, y, z: the identity
		Vec3<T> m_translation{T(0)};
	};

	using RigidTransformf = RigidTransform<float>;
	using RigidTransformd = RigidTransform<double>;

	// referenceFromAimed for a frame at `position` whose forward points at `target`, both in
	// `reference` coordinates, turned about forward so that its up is as near the reference's up as
	// forward allows. `aimed` says which of its own axes are forward and up: the camera frame by
	// default. nullopt when `target` is `position`, or lies straight above or below it, where no turn
	// about forward is the upright one.
	//
	// PRECONDITION: both conventions are right-handed (asserted in debug). A rigid transform cannot
	// relate frames of opposite handedness, and the cross products here assume a right-handed one.
	template <typename T>
	std::optional<RigidTransform<T>> lookingAt(const Vec3<T>& position, const Vec3<T>& target,
											   const AxisConvention& reference,
											   const AxisConvention& aimed = AxisConvention::XRightYDownZForward())
	{
		assert(reference.rightHanded() && aimed.rightHanded() && "lookingAt needs right-handed conventions");
		const Vec3<T> toTarget = target - position;
		if (dot(toTarget, toTarget) == T(0))
			return std::nullopt;
		Vec3<T> up{T(0)};
		for (length_t i = 0; i < 3; ++i)
		{
			const AxisDirection d = i == 0 ? reference.x() : i == 1 ? reference.y()
																	: reference.z();
			if (d == AxisDirection::Up || d == AxisDirection::Down)
				up[i] = d == AxisDirection::Up ? T(1) : T(-1);
		}
		const Vec3<T> forward = normalize(toTarget);
		const Vec3<T> side = cross(forward, up);
		if (dot(side, side) == T(0))
			return std::nullopt;
		const Vec3<T> right = normalize(side);
		// Each of the aimed frame's axes, in reference coordinates.
		const auto axis = [&](AxisDirection d) -> Vec3<T>
		{
			switch (d)
			{
				case AxisDirection::Right:
					return right;
				case AxisDirection::Left:
					return -right;
				case AxisDirection::Up:
					return cross(right, forward);
				case AxisDirection::Down:
					return cross(forward, right);
				case AxisDirection::Forward:
					return forward;
				case AxisDirection::Backward:
					return -forward;
			}
			return forward;
		};
		return RigidTransform<T>{quat_cast(Mat<3, 3, T>{axis(aimed.x()), axis(aimed.y()), axis(aimed.z())}), position};
	}
} // namespace lain::math
