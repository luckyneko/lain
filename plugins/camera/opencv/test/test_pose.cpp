// The OpenCV board pose solver through lain's seam (board::pose): exact on an exact view, the same
// whatever the distortion model, and, for a planar board far away and nearly square-on, BOTH of the
// plane's poses, each explaining the view within its noise. That last is the evidence registration
// needs to settle a flip with another camera (CONTEXT.md, "Pose solver"), and the reason the seam
// carries an alternative at all.

#include "syntheticview.h"

#include <lain/camera/board/pose.h>
#include <lain/camera/opencv/register.h>
#include <lain/camera/projection.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <random>

using namespace lain;
using namespace lain::camera;
namespace cb = lain::camera::board;
using Catch::Matchers::WithinAbs;

namespace
{
	void ensureBackend()
	{
		static const bool once = []
		{
			opencv::registerBackend();
			return true;
		}();
		(void)once;
	}

	cb::Specification boardSpec()
	{
		cb::PatternParameters p;
		p.dictionary = cb::Dictionary::Aruco5x5_100;
		p.squaresX = 7;
		p.squaresY = 5;
		p.markerToSquare = 0.75;
		cb::Instance instance;
		instance.identity = "synthetic";
		instance.squareLength.value = core::Length::from<core::Length::Millimetres>(24.0);
		return *cb::Specification::create(*cb::Pattern::create(p).pattern, instance).specification;
	}

	CameraModel cameraWith(const Distortion& distortion)
	{
		CameraModelParameters p = testing::pinhole();
		p.distortion = distortion;
		ModelResult result = CameraModel::create(p);
		REQUIRE(result.model.has_value());
		return *result.model;
	}

	// Gaussian noise of standard deviation `sigma`, by Box-Muller over mt19937_64, whose output the
	// standard fixes: std::normal_distribution is implementation-defined, and these views must be the
	// same on every platform.
	struct Noise
	{
		std::mt19937_64 engine;
		double sigma;

		double operator()()
		{
			const double u1 = (double(engine() >> 11) + 0.5) / 9007199254740992.0;
			const double u2 = double(engine() >> 11) / 9007199254740992.0;
			return sigma * std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
		}
	};

	// Every corner of the board at `pose`, projected through `model`, with Gaussian pixel noise.
	cb::Observation observe(const CameraModel& model, const cb::Specification& spec, const math::RigidTransformd& pose,
							double noise = 0.0, std::uint64_t seed = 1)
	{
		Noise jitter{std::mt19937_64(seed), noise};
		cb::Observation view;
		view.frame = {core::Uri{"/synthetic/pose"}, 0, {}};
		view.image = model.image();
		view.pattern = spec.pattern().fingerprint();
		for (std::uint32_t id = 0; id < spec.pattern().cornerCount(); ++id)
		{
			const math::Vec3d p = pose.apply(*spec.cornerPosition(id));
			const Projection<double> pixel = project(model, p.x, p.y, p.z);
			REQUIRE(pixel.ok());
			const double du = noise > 0 ? jitter() : 0.0;
			const double dv = noise > 0 ? jitter() : 0.0;
			view.features.push_back({id, {pixel.u + du, pixel.v + dv}, std::nullopt});
		}
		return view;
	}

	double rotationBetween(const math::RigidTransformd& a, const math::RigidTransformd& b)
	{
		const double w = std::abs(math::dot(a.rotation(), b.rotation()));
		return 2.0 * std::acos(std::min(1.0, w));
	}
} // namespace

TEST_CASE("the plugin registers a board pose solver", "[camera][opencv][pose]")
{
	ensureBackend();
	CHECK(cb::canSolvePose());
}

TEST_CASE("an exact view's pose is recovered, for every distortion model", "[camera][opencv][pose]")
{
	ensureBackend();
	const cb::Specification spec = boardSpec();
	const math::RigidTransformd truth = testing::cameraFromBoard(spec, 0.45, 0.35, -0.25);
	// Two models OpenCV can represent and one it cannot: the solve runs on rays lain unprojected, so
	// the model is lain's business.
	for (const Distortion& d : {Distortion{NoDistortion{}}, Distortion{BrownConrady5{-0.1, 0.05, 0.001, -0.0005, 0.0}},
								Distortion{InverseBrownConrady5{0.08, -0.02, 0.0005, 0.0003, 0.0}}})
	{
		const CameraModel model = cameraWith(d);
		CAPTURE(std::string(displayName(d)));
		const cb::PoseResult result = cb::pose(model, spec, observe(model, spec, truth));
		REQUIRE(result.ok());
		CHECK(result.provenance.backend == "opencv");
		CHECK(rotationBetween(result.pose->cameraFromBoard, truth) < 1e-7);
		CHECK(math::length(result.pose->cameraFromBoard.translation() - truth.translation()) < 1e-8);
		CHECK(result.pose->rmsAngle < 1e-8);
	}
}

TEST_CASE("a far, nearly square-on board comes back with its plane's other pose", "[camera][opencv][pose]")
{
	ensureBackend();
	const cb::Specification spec = boardSpec();
	const CameraModel model = cameraWith(NoDistortion{});
	// 2.5 m away and turned 4 degrees: the board spans about 60 px, where perspective barely tells
	// a lean one way from a lean the other, and half a pixel of noise hides the difference.
	const math::RigidTransformd truth = testing::cameraFromBoard(spec, 2.5, 0.07, 0.0);
	const cb::PoseResult result = cb::pose(model, spec, observe(model, spec, truth, 0.5, 7));
	REQUIRE(result.ok());
	REQUIRE(result.alternative.has_value());

	// The two are genuinely different poses, each explaining the view to within its half-pixel noise
	// (about 0.6 mrad at f = 900), as the true pose does: one view cannot choose between them. Measured
	// on this view: the two are 0.33 rad apart, and the backend's FIRST pose is the farther from the
	// truth (0.22 rad against 0.13), which is why a caller with more evidence must make the choice.
	CHECK(rotationBetween(result.pose->cameraFromBoard, result.alternative->cameraFromBoard) > 0.1);
	CHECK(result.pose->rmsAngle < 0.001);
	CHECK(result.alternative->rmsAngle < 0.001);
	const cb::ViewResidual truthResidual = cb::measure(model, spec, observe(model, spec, truth, 0.5, 7), truth);
	CHECK(std::sqrt(truthResidual.sumSquaredAngle / truthResidual.corners) < 0.001);
}

TEST_CASE("a pose reached twice is not reported as an alternative", "[camera][opencv][pose]")
{
	ensureBackend();
	const cb::Specification spec = boardSpec();
	const CameraModel model = cameraWith(NoDistortion{});
	// The same far board without noise: refining IPPE's second solution lands on the first, about
	// 3e-5 rad from it, so there is one pose and nothing to choose.
	const math::RigidTransformd truth = testing::cameraFromBoard(spec, 2.5, 0.07, 0.0);
	const cb::PoseResult result = cb::pose(model, spec, observe(model, spec, truth));
	REQUIRE(result.ok());
	CHECK_FALSE(result.alternative.has_value());
	CHECK(rotationBetween(result.pose->cameraFromBoard, truth) < 1e-7);
}

TEST_CASE("a view with fewer than four corners has no pose", "[camera][opencv][pose]")
{
	ensureBackend();
	const cb::Specification spec = boardSpec();
	const CameraModel model = cameraWith(NoDistortion{});
	cb::Observation view = observe(model, spec, testing::cameraFromBoard(spec, 0.45, 0.2, 0.1));
	view.features.resize(3);
	const cb::PoseResult result = cb::pose(model, spec, view);
	CHECK(result.status == cb::PoseStatus::TooFewCorners);
	CHECK_FALSE(result.pose.has_value());
}
