// CameraModel::create: the only door to a camera model, and what it refuses.

#include "testcamera.h"

#include <lain/camera/cameramodel.h>
#include <lain/camera/projection.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

TEST_CASE("a model scaled to another resolution sees every point along the ray it saw before", "[camera][model]")
{
	// Every pixel corner of the scaled image, and the point at the same fraction of the original, have
	// one ray: through all six models, at a half, an eighth, half as large again, and a new aspect.
	for (int m = 0; m < kModelCount; ++m)
	{
		const ModelResult original = CameraModel::create(parametersWith(everyModel(m)));
		REQUIRE(original.model);
		for (const ImageGeometry image : {ImageGeometry{320, 240}, ImageGeometry{80, 60}, ImageGeometry{960, 720}, ImageGeometry{400, 360}})
		{
			INFO("model " << m << ", " << image.width << " x " << image.height);
			const ModelResult result = scaled(*original.model, image);
			REQUIRE(result.model);
			CHECK(result.model->image() == image);
			CHECK(result.model->distortion().index() == original.model->distortion().index());
			const double sx = 640.0 / double(image.width);
			const double sy = 480.0 / double(image.height);
			double worst = 0.0;
			std::size_t without = 0; // corners either model has no ray at
			for (std::uint32_t j = 0; j <= image.height; ++j)
			{
				for (std::uint32_t i = 0; i <= image.width; ++i)
				{
					const auto a = unproject(*result.model, double(i) - 0.5, double(j) - 0.5);
					const auto b = unproject(*original.model, double(i) * sx - 0.5, double(j) * sy - 0.5);
					if (!a.ok() || !b.ok())
					{
						++without;
						continue;
					}
					// As unit rays: unproject's z need not be 1.
					const double la = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
					const double lb = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z);
					worst = std::max({worst, std::abs(a.x / la - b.x / lb), std::abs(a.y / la - b.y / lb), std::abs(a.z / la - b.z / lb)});
				}
			}
			CHECK(without == 0);
			CHECK(worst < 1e-12);
		}
	}

	// Of no pixels, it is no model.
	const ModelResult pinhole = CameraModel::create(parametersWith(NoDistortion{}));
	CHECK_FALSE(scaled(*pinhole.model, ImageGeometry{0, 240}).model);
}
