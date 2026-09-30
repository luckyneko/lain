// Formula-level golden values for every distortion model (ADR-0016). The expected numbers come from
// an independent implementation of the formulas as distortion.h writes them, at one camera-frame
// point with distinct coefficients, so a swapped coefficient, a reversed direction or a wrong term
// each move the answer.

#include "testcamera.h"

#include <lain/camera/projection.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace lain::camera;
using namespace lain::camera::testing;

namespace
{
	void requirePixel(const Projection<double>& p, double u, double v)
	{
		REQUIRE(p.ok());
		CHECK(p.u == Catch::Approx(u).margin(1e-9));
		CHECK(p.v == Catch::Approx(v).margin(1e-9));
	}
} // namespace

TEST_CASE("each distortion model projects to its formula's pixel", "[camera][distortion]")
{
	// The point (0.3, -0.2, 1): ideal normalised coordinates (0.3, -0.2).
	requirePixel(project(cameraWith(NoDistortion{}), 0.3, -0.2, 1.0), 500 * 0.3 + 320.5, 510 * -0.2 + 239.5);
	requirePixel(project(cameraWith(brownConrady()), 0.3, -0.2, 1.0), 471.95654549999995, 136.48744906);
	requirePixel(project(cameraWith(modifiedBrownConrady()), 0.3, -0.2, 1.0), 471.95066496753634, 136.49144782207526);
	requirePixel(project(cameraWith(rational()), 0.3, -0.2, 1.0), 471.5866905335673, 136.73895043717417);
	requirePixel(project(cameraWith(kannalaBrandt()), 0.3, -0.2, 1.0), 465.30603577852503, 141.03189567060298);
}

TEST_CASE("inverse Brown-Conrady maps a distorted pixel to its ideal ray in closed form", "[camera][distortion]")
{
	// Distorted normalised (0.3, -0.2) is pixel (470.5, 137.5); the polynomial takes it to the ideal
	// coordinates below, and the ray points along (x, y, 1).
	const Unprojection<double> ray = unproject(cameraWith(inverseBrownConrady()), 470.5, 137.5);
	REQUIRE(ray.ok());
	CHECK(ray.x / ray.z == Catch::Approx(0.3029130909999999).margin(1e-12));
	CHECK(ray.y / ray.z == Catch::Approx(-0.201985394).margin(1e-12));
}

TEST_CASE("forward and inverse Brown-Conrady are not interchangeable", "[camera][distortion]")
{
	// Five equal numbers, two different models: the same point lands on different pixels.
	const Projection<double> forward = project(cameraWith(brownConrady()), 0.3, -0.2, 1.0);
	const Projection<double> inverse = project(cameraWith(inverseBrownConrady()), 0.3, -0.2, 1.0);
	REQUIRE(forward.ok());
	REQUIRE(inverse.ok());
	CHECK(std::abs(forward.u - inverse.u) > 1.0);
}

TEST_CASE("every model has a display name", "[camera][distortion]")
{
	// Compared as std::string: Catch2 here cannot stringify a std::string_view, which fails to LINK,
	// and only under MSVC.
	CHECK(std::string(displayName(NoDistortion{})) == "no distortion");
	CHECK(std::string(displayName(BrownConrady5{})) == "Brown-Conrady 5");
	CHECK(std::string(displayName(InverseBrownConrady5{})) == "Inverse Brown-Conrady 5");
	CHECK(std::string(displayName(ModifiedBrownConrady5{})) == "Modified Brown-Conrady 5");
	CHECK(std::string(displayName(RationalBrownConrady8{})) == "Rational Brown-Conrady 8");
	CHECK(std::string(displayName(KannalaBrandt4{})) == "Kannala-Brandt 4");
}
