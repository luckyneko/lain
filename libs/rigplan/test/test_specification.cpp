// Specifications, and the nominal camera model a sensor and a lens derive.

#include "testplan.h"

#include <lain/rigplan/specification.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <variant>
#include <vector>

using lain::core::Length;
using lain::rigplan::CameraSpecification;
using lain::rigplan::LensSpecification;
using lain::rigplan::SensorSpecification;
using lain::rigplan::SpecificationDiagnostic;
using lain::rigplan::SpecificationProblem;
using lain::rigplan::Specifications;

using testplan::lens;
using testplan::sensor;

static Specifications valid()
{
	Specifications s;
	s.sensors.push_back(sensor());
	s.lenses.push_back(lens());
	s.cameras.push_back(CameraSpecification{"hd-4mm", "hd", "4mm"});
	return s;
}

static std::vector<SpecificationProblem> problems(const Specifications& s)
{
	std::vector<SpecificationProblem> out;
	for (const SpecificationDiagnostic& d : lain::rigplan::check(s))
		out.push_back(d.problem);
	return out;
}

TEST_CASE("a nominal model is the lens's focal length over the sensor's pitch, centred", "[rigplan][specification]")
{
	const lain::camera::ModelResult result = lain::rigplan::nominalModel(sensor(), lens());
	REQUIRE(result.model.has_value());
	const lain::camera::CameraModel& model = *result.model;

	REQUIRE(model.image() == lain::camera::ImageGeometry{1280, 720});
	REQUIRE(model.intrinsics().fx == 2000.0); // 4 mm / 2 um, exactly: both are whole nanometres
	REQUIRE(model.intrinsics().fy == 2000.0);
	// Pixel (0, 0) is the centre of the top-left pixel, so the image's centre is half a pixel short
	// of width / 2.
	REQUIRE(model.intrinsics().cx == 639.5);
	REQUIRE(model.intrinsics().cy == 359.5);

	const auto* distortion = std::get_if<lain::camera::BrownConrady5>(&model.distortion());
	REQUIRE(distortion != nullptr);
	REQUIRE(distortion->coefficients() == lain::camera::BrownConrady5{-0.05, 0.01, 0.0, 0.0, 0.0}.coefficients());
}

TEST_CASE("a lens whose distortion folds inside the sensor's image makes no nominal model", "[rigplan][specification]")
{
	// Under k1 alone a ray reaches a distorted radius of at most (2/3) sqrt(-1 / (3 k1)): 0.54 for
	// k1 = -0.5, far short of a 1 mm lens's corner on this sensor at 1.47 (fx = 500).
	LensSpecification wide = lens("1mm");
	wide.focalLength = Length::from<Length::Millimetres>(1.0);
	wide.distortion = lain::camera::BrownConrady5{-0.5, 0.0, 0.0, 0.0, 0.0};
	const lain::camera::ModelResult result = lain::rigplan::nominalModel(sensor(), wide);
	REQUIRE_FALSE(result.model.has_value());
	REQUIRE(result.diagnostics.front().problem == lain::camera::ModelProblem::FoldsInsideImage);

	// And the set that pairs them says so, naming the camera.
	Specifications s = valid();
	s.lenses.push_back(wide);
	s.cameras.push_back(CameraSpecification{"hd-1mm", "hd", "1mm"});
	const std::vector<SpecificationDiagnostic> diagnostics = lain::rigplan::check(s);
	REQUIRE(diagnostics.size() == 1);
	REQUIRE(diagnostics.front().problem == SpecificationProblem::InvalidNominalModel);
	REQUIRE(diagnostics.front().detail.find("'hd-1mm'") != std::string::npos);
}

TEST_CASE("a usable set of specifications has no problems, and finds by name", "[rigplan][specification]")
{
	const Specifications s = valid();
	REQUIRE(lain::rigplan::check(s).empty());
	REQUIRE(lain::rigplan::findSensor(s, "hd") == &s.sensors[0]);
	REQUIRE(lain::rigplan::findLens(s, "4mm") == &s.lenses[0]);
	REQUIRE(lain::rigplan::findCamera(s, "hd-4mm") == &s.cameras[0]);
	REQUIRE(lain::rigplan::findSensor(s, "4mm") == nullptr); // names are per kind
}

TEST_CASE("names must be given, and unique within their kind", "[rigplan][specification]")
{
	Specifications s = valid();
	s.sensors.push_back(sensor(""));
	s.lenses.push_back(lens("4mm"));
	s.cameras.push_back(CameraSpecification{"hd-4mm", "hd", "4mm"});
	s.lenses.push_back(lens("hd")); // the same name as a sensor is fine: a different kind
	REQUIRE(problems(s) == std::vector<SpecificationProblem>{SpecificationProblem::Unnamed, SpecificationProblem::DuplicateName,
															 SpecificationProblem::DuplicateName});
}

TEST_CASE("a camera must name a sensor and a lens the set has", "[rigplan][specification]")
{
	Specifications s = valid();
	s.cameras.push_back(CameraSpecification{"stray", "4k", "8mm"});
	REQUIRE(problems(s) == std::vector<SpecificationProblem>{SpecificationProblem::UnknownSensor, SpecificationProblem::UnknownLens});
}

TEST_CASE("a sensor needs an image and a positive pitch, and a lens a positive focal length", "[rigplan][specification]")
{
	Specifications s = valid();
	s.sensors[0].image = {0, 720};
	s.sensors[0].pixelPitch = Length{};
	s.lenses[0].focalLength = Length::from<Length::Millimetres>(-4.0);
	// The camera on them is not ALSO reported for the model it cannot make: one fault, one problem.
	REQUIRE(problems(s) == std::vector<SpecificationProblem>{SpecificationProblem::EmptyImage, SpecificationProblem::NonPositivePixelPitch,
															 SpecificationProblem::NonPositiveFocalLength});
}

TEST_CASE("a tolerance is a bound: never negative, always finite, and a focal one below 1", "[rigplan][specification]")
{
	SECTION("a negative principal-point tolerance")
	{
		Specifications s = valid();
		s.sensors[0].principalPointTolerance = Length::from<Length::Micrometres>(-1.0);
		REQUIRE(problems(s) == std::vector<SpecificationProblem>{SpecificationProblem::InvalidTolerance});
	}
	SECTION("a focal-length tolerance of 1, at which a unit could have no focal length")
	{
		Specifications s = valid();
		s.lenses[0].focalLengthTolerance = 1.0;
		REQUIRE(problems(s) == std::vector<SpecificationProblem>{SpecificationProblem::InvalidTolerance});
	}
	SECTION("a distortion tolerance that is not a number")
	{
		Specifications s = valid();
		s.lenses[0].distortionTolerance = std::numeric_limits<double>::quiet_NaN();
		REQUIRE(problems(s) == std::vector<SpecificationProblem>{SpecificationProblem::InvalidTolerance});
	}
	SECTION("a distortion tolerance of more than 1 is allowed: it only scales the distortion")
	{
		Specifications s = valid();
		s.lenses[0].distortionTolerance = 1.5;
		REQUIRE(problems(s).empty());
	}
}

TEST_CASE("a light needs a finite, non-negative output, and a spot a cone that widens", "[rigplan][specification]")
{
	Specifications s = valid();
	lain::rigplan::LightSpecification point;
	point.name = "bulb";
	point.innerConeDegrees = 90.0; // a point light has no cone, so its numbers are not checked
	point.outerConeDegrees = 10.0;
	s.lights.push_back(point);
	REQUIRE(problems(s).empty());

	SECTION("a negative intensity")
	{
		s.lights[0].intensity = -1.0;
		REQUIRE(problems(s) == std::vector<SpecificationProblem>{SpecificationProblem::InvalidLight});
	}
	SECTION("a spot whose cone narrows")
	{
		s.lights[0].kind = lain::rigplan::LightKind::Spot;
		REQUIRE(problems(s) == std::vector<SpecificationProblem>{SpecificationProblem::InvalidLight});
	}
	SECTION("a spot that shines behind itself")
	{
		s.lights[0].kind = lain::rigplan::LightKind::Spot;
		s.lights[0].innerConeDegrees = 30.0;
		s.lights[0].outerConeDegrees = 180.0;
		REQUIRE(problems(s) == std::vector<SpecificationProblem>{SpecificationProblem::InvalidLight});
	}
	SECTION("a spot of 20 to 30 degrees is fine")
	{
		s.lights[0].kind = lain::rigplan::LightKind::Spot;
		s.lights[0].innerConeDegrees = 20.0;
		s.lights[0].outerConeDegrees = 30.0;
		REQUIRE(problems(s).empty());
	}
}
