// The procedural texture: a seeded function of position, and the image it bakes to.

#include <lain/image/color.h>
#include <lain/image/imageview.h>
#include <lain/image/procedural.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using lain::image::ProceduralTexture;

TEST_CASE("the procedural texture is the function M9's synthetic scene drew with", "[image][procedural]")
{
	// Expected values from an independent reimplementation (Python, from the same definition). Compared
	// to 1e-9 rather than exactly, since a compiler may fuse a multiply and an add where Python does not;
	// that the lift itself changed no bit was checked on M9's rendered footage, 152 files byte for byte.
	REQUIRE(std::abs(lain::image::proceduralGrey({1, 0.04}, 0.0, 0.0) - 98.39219388362774) < 1e-9);
	REQUIRE(std::abs(lain::image::proceduralGrey({1, 0.04}, 0.137, 0.291) - 120.83171732321783) < 1e-9);
	REQUIRE(std::abs(lain::image::proceduralGrey({77, 0.03}, -1.25, 2.5) - 131.65248663934247) < 1e-9);
	REQUIRE(std::abs(lain::image::proceduralGrey({202, 0.04}, 3.3, -0.7) - 92.80372864347224) < 1e-9);
	// The discs cover a few percent of a surface, so points chosen at random miss them all, and a change
	// to them would pass unseen. These two sit in a disc's soft edge, where the noise and the disc mix.
	REQUIRE(std::abs(lain::image::proceduralGrey({1, 0.04}, 0.1257274010882042, 0.09869775928900835) - 55.386597770636) < 1e-9);
	REQUIRE(std::abs(lain::image::proceduralGrey({1, 0.04}, 0.701175600026311, -0.2893596682819062) - 210.12031632233962) < 1e-9);
}

TEST_CASE("a procedural texture stays within grey and depends on its seed", "[image][procedural]")
{
	bool differs = false;
	for (int k = 0; k < 500; ++k)
	{
		const double u = 0.013 * k, v = -0.007 * k;
		const double a = lain::image::proceduralGrey({5, 0.04}, u, v);
		REQUIRE(a >= 0.0);
		REQUIRE(a <= 255.0);
		differs = differs || a != lain::image::proceduralGrey({6, 0.04}, u, v);
	}
	REQUIRE(differs);
}

TEST_CASE("a baked texture holds the texture at each texel's centre", "[image][procedural]")
{
	const ProceduralTexture texture{9, 0.04};
	const lain::image::Image baked = lain::image::bakeProceduralTexture(texture, 16, 8, 100.0);
	REQUIRE(baked.width() == 16);
	REQUIRE(baked.height() == 8);
	REQUIRE(baked.pixelFormat() == lain::image::PixelFormat::Gray32F);
	REQUIRE(baked.colorSpace() == lain::image::ColorSpace::Linear); // reflectance, sampled as linear
	const auto texels = baked.as<lain::image::ColorGrayf>();
	for (int y = 0; y < 8; ++y)
	{
		for (int x = 0; x < 16; ++x)
		{
			const double expected = lain::image::proceduralGrey(texture, (x + 0.5) / 100.0, (y + 0.5) / 100.0) / 255.0;
			REQUIRE(texels(x, y).r == float(expected));
		}
	}
}
