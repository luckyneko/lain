#pragma once

#include "lain/math/types.h"

#include <cstdint>
#include <optional>
#include <string>

namespace lain::math
{
	// A physical direction an axis can point in.
	enum class AxisDirection : std::uint8_t
	{
		Right,
		Left,
		Up,
		Down,
		Forward,
		Backward,
	};

	// Which physical direction each coordinate axis points in: orientation and handedness, with no
	// origin or scale (CONTEXT.md, "Axis convention"). A name states the directions outright, as in
	// XRightYDownZForward, instead of borrowing a library's name ("the OpenCV convention") that says
	// nothing to someone who has not memorised it.
	//
	// Always VALID: the three axes lie along three different physical axes. create() is the door, so
	// basisChange never meets two axes pointing along one line.
	class AxisConvention
	{
	public:
		// The convention for these axis directions, or nullopt when two of them lie on one physical
		// axis (Right with Left, or Up with Up).
		static constexpr std::optional<AxisConvention> create(AxisDirection x, AxisDirection y, AxisDirection z)
		{
			const int a = physicalAxis(x), b = physicalAxis(y), c = physicalAxis(z);
			if (a == b || b == c || a == c)
				return std::nullopt;
			return AxisConvention{x, y, z};
		}

		// The camera frame: every camera calibration and registration interface speaks it.
		static constexpr AxisConvention XRightYDownZForward()
		{
			return {AxisDirection::Right, AxisDirection::Down, AxisDirection::Forward};
		}
		// The usual graphics camera frame, looking down -Z.
		static constexpr AxisConvention XRightYUpZBackward()
		{
			return {AxisDirection::Right, AxisDirection::Up, AxisDirection::Backward};
		}

		constexpr AxisDirection x() const { return m_x; }
		constexpr AxisDirection y() const { return m_y; }
		constexpr AxisDirection z() const { return m_z; }

		// Whether x cross y is z, measured physically.
		constexpr bool rightHanded() const
		{
			// In the right-handed (Right, Up, Backward) basis each direction is a signed unit axis,
			// so the determinant of the three columns reduces to the sign of the permutation they
			// form times the product of their signs.
			const int p[3] = {physicalAxis(m_x), physicalAxis(m_y), physicalAxis(m_z)};
			int inversions = 0;
			for (int i = 0; i < 3; ++i)
			{
				for (int j = i + 1; j < 3; ++j)
					inversions += p[i] > p[j] ? 1 : 0;
			}
			const int sign = (inversions % 2 == 0 ? 1 : -1) * sign3(m_x) * sign3(m_y) * sign3(m_z);
			return sign > 0;
		}

		// "XRightYDownZForward": the name this convention would have as a named constant.
		std::string toString() const { return "X" + name(m_x) + "Y" + name(m_y) + "Z" + name(m_z); }

		constexpr bool operator==(const AxisConvention& rhs) const
		{
			return m_x == rhs.m_x && m_y == rhs.m_y && m_z == rhs.m_z;
		}
		constexpr bool operator!=(const AxisConvention& rhs) const { return !(*this == rhs); }

		// The direction as a unit vector in the right-handed (Right, Up, Backward) basis, the common
		// ground basisChange measures both conventions against.
		template <typename T>
		static Vec3<T> vectorOf(AxisDirection direction)
		{
			Vec3<T> out{T(0)};
			out[physicalAxis(direction)] = T(sign3(direction));
			return out;
		}

	private:
		constexpr AxisConvention(AxisDirection x, AxisDirection y, AxisDirection z)
			: m_x(x)
			, m_y(y)
			, m_z(z)
		{
		}

		// Which physical axis a direction lies on in the (Right, Up, Backward) basis: 0, 1 or 2.
		static constexpr int physicalAxis(AxisDirection d)
		{
			switch (d)
			{
				case AxisDirection::Right:
				case AxisDirection::Left:
					return 0;
				case AxisDirection::Up:
				case AxisDirection::Down:
					return 1;
				case AxisDirection::Forward:
				case AxisDirection::Backward:
					return 2;
			}
			return 0;
		}

		// Which way along that axis: Right, Up and Backward are positive.
		static constexpr int sign3(AxisDirection d)
		{
			return (d == AxisDirection::Right || d == AxisDirection::Up || d == AxisDirection::Backward) ? 1 : -1;
		}

		static std::string name(AxisDirection d)
		{
			switch (d)
			{
				case AxisDirection::Right:
					return "Right";
				case AxisDirection::Left:
					return "Left";
				case AxisDirection::Up:
					return "Up";
				case AxisDirection::Down:
					return "Down";
				case AxisDirection::Forward:
					return "Forward";
				case AxisDirection::Backward:
					return "Backward";
			}
			return "?";
		}

		AxisDirection m_x;
		AxisDirection m_y;
		AxisDirection m_z;
	};

	// The matrix that re-expresses coordinates written in `from` as coordinates in `to`: a pure
	// basis change, never a camera operation. It is a signed permutation, so it is exact, and its
	// inverse is its transpose, basisChange(to, from). Its determinant is -1 exactly when the two
	// conventions differ in handedness.
	template <typename T>
	Mat<3, 3, T> basisChange(const AxisConvention& from, const AxisConvention& to)
	{
		// Columns are each axis's direction in the common basis, so fromCommon maps `from`
		// coordinates to common ones, and the transpose of the orthonormal toCommon undoes `to`.
		const Mat<3, 3, T> fromCommon{AxisConvention::vectorOf<T>(from.x()), AxisConvention::vectorOf<T>(from.y()),
									  AxisConvention::vectorOf<T>(from.z())};
		const Mat<3, 3, T> toCommon{AxisConvention::vectorOf<T>(to.x()), AxisConvention::vectorOf<T>(to.y()),
									AxisConvention::vectorOf<T>(to.z())};
		return transpose(toCommon) * fromCommon;
	}
} // namespace lain::math
