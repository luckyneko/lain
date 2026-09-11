// lain::math::Rect2 — the axis-aligned region. The one OWNED type in lain::math (GLM has no
// rectangle), so unlike the aliases beside it there is behaviour here to pin down.

#include "lain/math/rect.h"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace lm = lain::math;

TEST_CASE("a rect is an origin and an extent, addressable either way", "[math][rect]")
{
	STATIC_REQUIRE(std::is_same_v<lm::Rect2<int>, lm::Rect2i>);

	// The two constructors must agree: the four-int form exists because that is how a caller
	// already holds a region, and it would be worth nothing if it disagreed with the vector form.
	constexpr lm::Rect2i loose{1, 2, 3, 4};
	const lm::Rect2i vectors{lm::Vec2i{1, 2}, lm::Vec2i{3, 4}};
	REQUIRE(loose == vectors);

	STATIC_REQUIRE(loose.origin.x == 1);
	STATIC_REQUIRE(loose.origin.y == 2);
	STATIC_REQUIRE(loose.extent.x == 3);
	STATIC_REQUIRE(loose.extent.y == 4);
}

TEST_CASE("right and bottom are the HALF-OPEN ends", "[math][rect]")
{
	// One past the last covered column/row, not the last one. Off by one here would read as an
	// off-by-one in every bounds check written against it — which is the whole job of the type.
	constexpr lm::Rect2i rect{10, 20, 4, 5};
	STATIC_REQUIRE(rect.right() == 14);
	STATIC_REQUIRE(rect.bottom() == 25);

	// So a rect exactly filling a 14x25 image is in bounds, and one pixel more is not.
	STATIC_REQUIRE(lm::Rect2i{0, 0, 14, 25}.right() == 14);
	STATIC_REQUIRE(lm::Rect2i{0, 0, 15, 25}.right() == 15);
}

TEST_CASE("an extent of zero or less covers nothing", "[math][rect]")
{
	STATIC_REQUIRE_FALSE(lm::Rect2i{0, 0, 1, 1}.empty());
	STATIC_REQUIRE(lm::Rect2i{}.empty()); // default-constructed covers nothing
	STATIC_REQUIRE(lm::Rect2i{5, 5, 0, 3}.empty());
	STATIC_REQUIRE(lm::Rect2i{5, 5, 3, 0}.empty());

	// A NEGATIVE extent is empty rather than reversed — reversing is an operation on the thing
	// being addressed, not a property of the address. A rect that quietly meant [x+w, x) would
	// make image::crop's bounds check pass and then read backwards off the buffer.
	STATIC_REQUIRE(lm::Rect2i{5, 5, -3, 3}.empty());
	STATIC_REQUIRE(lm::Rect2i{5, 5, 3, -3}.empty());
}

TEST_CASE("rects compare on both halves", "[math][rect]")
{
	constexpr lm::Rect2i rect{1, 2, 3, 4};
	STATIC_REQUIRE(rect == lm::Rect2i{1, 2, 3, 4});
	STATIC_REQUIRE(rect != lm::Rect2i{1, 2, 3, 5}); // extent differs
	STATIC_REQUIRE(rect != lm::Rect2i{0, 2, 3, 4}); // origin differs
}
