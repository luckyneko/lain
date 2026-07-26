// Unit tests for the typed-view types — as<C>() reinterpretation, range-for, subview.

#include "lain/image/color.h"
#include "lain/image/imageview.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using namespace lain::image;

TEST_CASE("as<C>() aliases the Image's bytes as typed pixels", "[view]")
{
	Image img(2, 2, PixelFormat::RGBA8);
	ImageView<ColorRGBA8> v = img.as<ColorRGBA8>();
	REQUIRE(v.width() == 2);
	REQUIRE(v.height() == 2);

	v(0, 0) = ColorRGBA8(1, 2, 3, 4);
	v(1, 1) = ColorRGBA8(5, 6, 7, 8);

	// The writes land in the underlying byte buffer (same storage, no copy).
	REQUIRE(img.data()[0] == 1);
	REQUIRE(img.data()[3] == 4);
	REQUIRE(img.data()[12] == 5); // pixel (1,1) -> 3rd pixel -> byte 12
	REQUIRE(img.data()[15] == 8);
}

TEST_CASE("range-for iterates a view's pixels", "[view]")
{
	Image img(2, 2, PixelFormat::Gray8);
	std::uint8_t n = 1;
	for (ColorGray8& p : img.as<ColorGray8>())
		p = ColorGray8(n++);
	REQUIRE(img.data()[0] == 1);
	REQUIRE(img.data()[1] == 2);
	REQUIRE(img.data()[2] == 3);
	REQUIRE(img.data()[3] == 4);
}

TEST_CASE("range-for over a subview visits only the window (stride-aware)", "[view]")
{
	Image img(4, 4, PixelFormat::Gray8); // all zero
	auto window = img.as<ColorGray8>().subview(1, 1, 2, 2);
	for (ColorGray8& p : window)
		p = ColorGray8(255);

	// Exactly the 2x2 window at (1,1)..(2,2) is set; the parent's row padding is untouched.
	int set = 0;
	for (int y = 0; y < 4; ++y)
	{
		for (int x = 0; x < 4; ++x)
		{
			if (img.data()[static_cast<std::size_t>(y) * 4 + x] == 255)
			{
				++set;
				REQUIRE(x >= 1);
				REQUIRE(x <= 2);
				REQUIRE(y >= 1);
				REQUIRE(y <= 2);
			}
		}
	}
	REQUIRE(set == 4);
}
