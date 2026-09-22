#pragma once

// lain::math::Rect2 — an axis-aligned rectangle as an origin and an extent.
//
// OWNED lain code, not a GLM alias: GLM models vectors and matrices and has no rectangle, so this
// is the one type here that is not a typed name over something else. It lives in its own header
// rather than in types.h for exactly that reason — types.h says what it is ("typed names over
// GLM") and this would make that untrue.
//
// It exists because a region was being passed as four loose ints (image::crop, ImageView::subview),
// where transposing x with y, or w with h, compiles and is silent.

#include "lain/math/types.h"

namespace lain::math
{
	// The region covered by `extent` pixels/units starting at `origin` — half-open, so
	// [origin, origin + extent). An extent of zero or less covers nothing; see empty().
	//
	// Origin-and-extent rather than two corners, because that is what both of the callers that
	// wanted this type already held, and because it makes "how big is it" a member rather than a
	// subtraction each reader performs for itself.
	template <typename T>
	struct Rect2
	{
		Vec2<T> origin{};
		Vec2<T> extent{};

		constexpr Rect2() = default;
		constexpr Rect2(Vec2<T> rectOrigin, Vec2<T> rectExtent)
			: origin(rectOrigin)
			, extent(rectExtent)
		{
		}
		constexpr Rect2(T x, T y, T width, T height)
			: origin(x, y)
			, extent(width, height)
		{
		}

		// One past the last column / row covered — the half-open ends.
		constexpr T right() const { return origin.x + extent.x; }
		constexpr T bottom() const { return origin.y + extent.y; }

		// Whether this covers nothing. A negative extent is empty rather than reversed: reversing
		// is an operation on the thing being addressed, not a property of the address.
		constexpr bool empty() const { return extent.x <= T{0} || extent.y <= T{0}; }

		constexpr bool operator==(const Rect2& other) const
		{
			return origin == other.origin && extent == other.extent;
		}
		constexpr bool operator!=(const Rect2& other) const { return !(*this == other); }
	};

	// The one concrete a consumer has asked for. Others (Rect2f, …) are one alias each when one
	// does — the same rule the port-type and codec registries follow.
	using Rect2i = Rect2<int>;
} // namespace lain::math
