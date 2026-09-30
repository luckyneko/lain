// CameraModel::create: the only door to a camera model, and what it refuses.

#include "testcamera.h"

#include <lain/camera/cameramodel.h>

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace lain::camera;
using namespace lain::camera::testing;

namespace
{
	bool reports(const ModelResult& result, ModelProblem problem)
	{
		for (const ModelDiagnostic& d : result.diagnostics)
		{
			if (d.problem == problem)
				return true;
		}
		return false;
	}
} // namespace

TEST_CASE("valid parameters make a model that keeps them", "[camera][model]")
{
	const ModelResult result = CameraModel::create(parametersWith(brownConrady()));
	REQUIRE(result.model.has_value());
	CHECK(result.diagnostics.empty());
	CHECK(result.model->image() == ImageGeometry{640, 480});
	CHECK(result.model->intrinsics().fy == 510.0);
	CHECK(std::get<BrownConrady5>(result.model->distortion()).p2 == -0.002);
}

TEST_CASE("create refuses malformed parameters and says why", "[camera][model]")
{
	SECTION("an empty image")
	{
		CameraModelParameters p = parametersWith(NoDistortion{});
		p.image.height = 0;
		const ModelResult result = CameraModel::create(p);
		CHECK_FALSE(result.model.has_value());
		CHECK(reports(result, ModelProblem::EmptyImage));
	}
	SECTION("a focal length that is zero or negative")
	{
		CameraModelParameters p = parametersWith(NoDistortion{});
		p.intrinsics.fx = 0.0;
		p.intrinsics.fy = -500.0;
		const ModelResult result = CameraModel::create(p);
		CHECK_FALSE(result.model.has_value());
		CHECK(result.diagnostics.size() == 2); // both named, not only the first
		CHECK(reports(result, ModelProblem::NonPositiveFocalLength));
	}
	SECTION("a NaN or an infinity anywhere")
	{
		CameraModelParameters p = parametersWith(BrownConrady5{0.1, std::numeric_limits<double>::infinity(), 0, 0, 0});
		p.intrinsics.cx = std::numeric_limits<double>::quiet_NaN();
		const ModelResult result = CameraModel::create(p);
		CHECK_FALSE(result.model.has_value());
		CHECK(result.diagnostics.size() == 2);
		CHECK(reports(result, ModelProblem::NonFiniteParameter));
	}
	SECTION("a distortion that folds before the image corners")
	{
		// The barrel fold sits at ideal radius 1.054, distorted radius 0.703; with fx = fy = 200 the
		// corners are at a distorted radius of 2.
		CameraModelParameters p = parametersWith(BrownConrady5{-0.3, 0, 0, 0, 0});
		p.intrinsics = {200.0, 200.0, 320.0, 240.0};
		const ModelResult result = CameraModel::create(p);
		CHECK_FALSE(result.model.has_value());
		CHECK(result.diagnostics.size() == 4); // every corner
		CHECK(reports(result, ModelProblem::FoldsInsideImage));
	}
}

TEST_CASE("a model applies to its own geometry only as Unknown", "[camera][model]")
{
	const CameraModel model = cameraWith(NoDistortion{});
	CHECK(applicability(model, ImageGeometry{640, 480}) == Applicability::Unknown);
	CHECK(applicability(model, ImageGeometry{1280, 720}) == Applicability::Incompatible);
	CHECK(applicability(model, ImageGeometry{480, 640}) == Applicability::Incompatible);
}
