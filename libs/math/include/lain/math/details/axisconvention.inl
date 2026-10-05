#pragma once

// The bodies of AxisConvention's helpers and of basisChange (see axisconvention.h). Split out so the
// header reads as the interface; lain::math is header-only and most of these are constexpr, so a .cpp
// is not an option.

#include <string>

namespace lain::math
{
	constexpr bool AxisConvention::rightHanded() const
	{
		// In the right-handed (Right, Up, Backward) basis each direction is a signed unit axis, so
		// the determinant of the three columns reduces to the sign of the permutation they form times
		// the product of their signs.
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

	template <typename T>
	Vec3<T> AxisConvention::vectorOf(AxisDirection direction)
	{
		Vec3<T> out{T(0)};
		out[physicalAxis(direction)] = T(sign3(direction));
		return out;
	}

	constexpr int AxisConvention::physicalAxis(AxisDirection d)
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

	constexpr int AxisConvention::sign3(AxisDirection d)
	{
		return (d == AxisDirection::Right || d == AxisDirection::Up || d == AxisDirection::Backward) ? 1 : -1;
	}

	inline std::string AxisConvention::name(AxisDirection d)
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
