// Unit tests for the traversal layer — forEach, transform, forEachBlock, visit.

#include "lain/image/traverse.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <type_traits>

using namespace lain::image;

TEST_CASE("forEach mutates every pixel in place", "[traverse]")
{
	Image img(2, 2, PixelFormat::RGBA8);
	forEach(img.as<ColorRGBA8>(), [](ColorRGBA8& p)
			{ p = ColorRGBA8(9, 9, 9, 9); });
	REQUIRE(img.data()[0] == 9);
	REQUIRE(img.data()[15] == 9);
}

TEST_CASE("transform maps src pixels into dst", "[traverse]")
{
	Image src(2, 1, PixelFormat::Gray8);
	src.as<ColorGray8>()(0, 0) = ColorGray8(10);
	src.as<ColorGray8>()(1, 0) = ColorGray8(20);

	Image dst(2, 1, PixelFormat::Gray8);
	transform(src.as<ColorGray8>(), dst.as<ColorGray8>(),
			  [](const ColorGray8& p)
			  { return ColorGray8(static_cast<std::uint8_t>(p[0] * 2)); });

	REQUIRE(dst.data()[0] == 20);
	REQUIRE(dst.data()[1] == 40);
}

TEST_CASE("forEachBlock tiles a view into clamped windows", "[traverse]")
{
	Image img(4, 4, PixelFormat::Gray8);
	int blocks = 0;
	forEachBlock(img.as<ColorGray8>(), 2, 2,
				 [&](ImageView<ColorGray8> block, int x0, int y0)
				 {
					 ++blocks;
					 REQUIRE(block.width() == 2);
					 REQUIRE(block.height() == 2);
					 // Paint the block with its origin so we can check placement.
					 forEach(block, [&](ColorGray8& p)
							 { p = ColorGray8(static_cast<std::uint8_t>(x0 + y0)); });
				 });
	REQUIRE(blocks == 4);
	// Bottom-right block origin (2,2) -> value 4 at pixel (3,3) -> byte 3*4 + 3 = 15.
	REQUIRE(img.data()[15] == 4);
}

TEST_CASE("forEachBlock clamps partial edge tiles", "[traverse]")
{
	Image img(3, 3, PixelFormat::Gray8);
	int maxRight = 0;
	forEachBlock(img.as<ColorGray8>(), 2, 2,
				 [&](ImageView<ColorGray8> block, int x0, int)
				 { maxRight = std::max(maxRight, x0 + block.width()); });
	REQUIRE(maxRight == 3); // right edge tile is width 1, not 2
}

TEST_CASE("visit dispatches to the Color matching the runtime format", "[traverse]")
{
	Image img(1, 1, PixelFormat::RGB8);
	// The generic lambda is instantiated per format; only the RGB8 branch runs here.
	visit(img, [](auto view)
		  {
			using C = std::remove_reference_t<decltype(view(0, 0))>;
			view(0, 0) = C(); // default pixel
			REQUIRE(descriptor(C::format).channelCount() == 3); });
	REQUIRE(img.pixelFormat() == PixelFormat::RGB8);
}

TEST_CASE("visit over a const image yields read-only views", "[traverse]")
{
	Image img(1, 1, PixelFormat::RGBA8);
	img.as<ColorRGBA8>()(0, 0) = ColorRGBA8(7, 8, 9, 10);

	const Image& cimg = img;
	int channels = 0;
	visit(cimg, [&](auto view)
		  {
			using C = std::remove_const_t<std::remove_reference_t<decltype(view(0, 0))>>;
			channels = descriptor(C::format).channelCount();
			REQUIRE(view(0, 0).r == 7); });
	REQUIRE(channels == 4);
}
