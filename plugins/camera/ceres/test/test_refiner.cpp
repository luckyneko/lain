// The Ceres registration refiner: the refinement itself (whitening, the reference held, pose-only
// problems), then the board registration method module running through it on the synthetic rig
// (libs/camera/test/syntheticrig.h) with pixel noise, where the stand-in refiner of
// test-camera-registration only handed back its start. Every tolerance is a measurement, written
// beside its check.

#include "syntheticrig.h"

#include <lain/camera/ceres/register.h>
#include <lain/camera/registration/board.h>
#include <lain/camera/registration/refiner.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <string>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::registration;
using namespace lain::camera::testing;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
namespace method = lain::camera::registration::board;

namespace
{
	void ensureCeres()
	{
		static const bool once = []
		{
			lain::camera::ceres::registerBackend();
			return true;
		}();
		(void)once;
		REQUIRE(refinerRegistry().keys() == std::vector<std::string>{"ceres"});
	}

	double rotationBetween(const math::RigidTransformd& a, const math::RigidTransformd& b)
	{
		const math::Quatd r = math::conjugate(a.rotation()) * b.rotation();
		return 2.0 * std::atan2(std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z), std::abs(r.w));
	}

	std::size_t indexOf(const capture::CameraIdentity& camera)
	{
		for (std::size_t c = 0; c < rig::scene().referenceFromCamera.size(); ++c)
		{
			if (rig::identityOf(c) == camera.value)
				return c;
		}
		FAIL("no rig camera is " << camera.value);
		return 0;
	}

	// The worst camera's rotation and translation error against the truth relative to the reference.
	std::pair<double, double> worstError(const Report& report)
	{
		REQUIRE(report.status == RegistrationStatus::Succeeded);
		const std::size_t reference = indexOf(*report.reference);
		double rotation = 0, translation = 0;
		for (const RegisteredCamera& c : report.cameras)
		{
			const math::RigidTransformd truth = rig::trueReferenceFromCamera(reference, indexOf(c.camera));
			rotation = std::max(rotation, rotationBetween(c.referenceFromCamera, truth));
			translation = std::max(translation, math::length(c.referenceFromCamera.translation() - truth.translation()));
		}
		return {rotation, translation};
	}

	Report run(const Request& request = {})
	{
		return method::registerCameras(rig::cameras(), rig::groups(), specification(), request);
	}

	// One camera at the reference, one board 1 m in front, and its corners where the camera sees them.
	Problem singleView()
	{
		const camera::board::Specification spec = specification();
		Problem problem;
		problem.models = {rig::model()};
		problem.cameraFromReference = {math::RigidTransformd{}};
		const math::Vec3d centre = rig::centroid(spec);
		const math::RigidTransformd body{math::Quatd{1, 0, 0, 0}, math::Vec3d{0, 0, 1} - centre};
		problem.referenceFromBody = {body};
		for (std::uint32_t id = 0; id < spec.pattern().cornerCount(); ++id)
		{
			const math::Vec3d corner = *spec.cornerPosition(id);
			const math::Vec3d p = body.apply(corner);
			const Projection<double> pixel = project(problem.models[0], p.x, p.y, p.z);
			REQUIRE(pixel.ok());
			problem.observations.push_back({0, 0, corner, {pixel.u, pixel.v}, std::nullopt});
		}
		problem.loss.family = LossFamily::None;
		return problem;
	}
} // namespace

TEST_CASE("the refiner whitens each corner by its covariance, else by the noise model", "[camera][ceres]")
{
	ensureCeres();
	// Move the board 1 mm right: every corner's residual is then about (0.5, 0) px at f = 500.
	Problem problem = singleView();
	problem.freeCameras = false;
	problem.maximumIterations = 0;
	const math::RigidTransformd truth = problem.referenceFromBody[0];
	problem.referenceFromBody[0] = math::RigidTransformd{truth.rotation(), truth.translation() + math::Vec3d{0.001, 0, 0}};

	double expected = 0;
	for (const RefinementObservation& o : problem.observations)
	{
		const math::Vec3d p = problem.referenceFromBody[0].apply(o.point);
		const Projection<double> predicted = project(problem.models[0], p.x, p.y, p.z);
		const math::Vec2d r = math::Vec2d{predicted.u, predicted.v} - o.pixel;
		expected += math::dot(r, r);
	}

	SECTION("by the noise model's sigma")
	{
		problem.noise.pixelSigma = 0.5;
		const Solution solution = refine(problem);
		CHECK_THAT(solution.initialCost, WithinRel(0.5 * expected / 0.25, 1e-9));
	}
	SECTION("by a covariance, through its Cholesky factor")
	{
		// An anisotropic, correlated covariance: the cost is 0.5 r^T C^-1 r, summed.
		const std::array<double, 3> c{0.04, 0.01, 0.09};
		double whitened = 0;
		for (RefinementObservation& o : problem.observations)
		{
			o.covariance = c;
			const math::Vec3d p = problem.referenceFromBody[0].apply(o.point);
			const Projection<double> predicted = project(problem.models[0], p.x, p.y, p.z);
			const math::Vec2d r = math::Vec2d{predicted.u, predicted.v} - o.pixel;
			const double det = c[0] * c[2] - c[1] * c[1];
			whitened += (c[2] * r.x * r.x - 2 * c[1] * r.x * r.y + c[0] * r.y * r.y) / det;
		}
		const Solution solution = refine(problem);
		CHECK_THAT(solution.initialCost, WithinRel(0.5 * whitened, 1e-9));
	}
}

TEST_CASE("a pose-only refinement moves the board and holds every camera", "[camera][ceres]")
{
	ensureCeres();
	Problem problem = singleView();
	problem.freeCameras = false;
	const math::RigidTransformd truth = problem.referenceFromBody[0];
	problem.cameraFromReference[0] =
		math::RigidTransformd{math::angleAxis(1e-3, math::Vec3d{0, 1, 0}), math::Vec3d{0.002, 0, 0}};
	problem.referenceFromBody[0] = math::RigidTransformd{math::angleAxis(0.05, math::Vec3d{1, 0, 0}) * truth.rotation(),
														 truth.translation() + math::Vec3d{0.01, -0.02, 0.03}};
	// The camera is held where it was put, wrong or not, and the board moves to explain the view.
	const Solution solution = refine(problem);
	REQUIRE(solution.usable());
	CHECK(rotationBetween(solution.cameraFromReference[0], problem.cameraFromReference[0]) < 1e-12);
	CHECK(math::length(solution.cameraFromReference[0].translation() - problem.cameraFromReference[0].translation()) < 1e-12);
	const math::RigidTransformd seen = solution.cameraFromReference[0] * solution.referenceFromBody[0];
	// The solver stops at its function tolerance: measured 3.4e-9 rad.
	CHECK(rotationBetween(seen, truth) < 1e-7);
	CHECK(math::length(seen.translation() - truth.translation()) < 1e-7);
	CHECK(solution.provenance.backend == "ceres");
	CHECK(solution.finalCost < 1e-12);
}

TEST_CASE("a pose-only refinement holds a camera that is not the reference too", "[camera][ceres]")
{
	ensureCeres();
	// A second camera 0.2 m to the side sees the same board, but is held 2 mrad from where it really
	// is. Held, it cannot move to agree with the first camera, so the board settles between them and
	// the cost stays well above zero; a refinement that freed it would fit both views exactly.
	Problem problem = singleView();
	problem.freeCameras = false;
	const math::RigidTransformd body = problem.referenceFromBody[0];
	const math::RigidTransformd secondTruth = rig::lookingAt(math::Vec3d{0.2, 0, 0}, math::Vec3d{0, 0, 1}).inverse();
	problem.models.push_back(problem.models[0]);
	problem.cameraFromReference.push_back(math::RigidTransformd{math::angleAxis(0.002, math::Vec3d{0, 1, 0}), math::Vec3d{0.0}} *
										  secondTruth);
	const std::size_t first = problem.observations.size();
	for (std::size_t i = 0; i < first; ++i)
	{
		const math::Vec3d p = (secondTruth * body).apply(problem.observations[i].point);
		const Projection<double> pixel = project(problem.models[1], p.x, p.y, p.z);
		REQUIRE(pixel.ok());
		problem.observations.push_back({1, 0, problem.observations[i].point, {pixel.u, pixel.v}, std::nullopt});
	}
	const Solution solution = refine(problem);
	REQUIRE(solution.usable());
	for (std::size_t c = 0; c < 2; ++c)
	{
		CAPTURE(c);
		CHECK(rotationBetween(solution.cameraFromReference[c], problem.cameraFromReference[c]) < 1e-12);
		CHECK(math::length(solution.cameraFromReference[c].translation() - problem.cameraFromReference[c].translation()) <
			  1e-12);
	}
	CHECK(solution.finalCost > 1.0);
}

TEST_CASE("a refinement holds its reference where it started, and moves the rest", "[camera][ceres]")
{
	ensureCeres();
	// The rig's truth in its own frame, with camera 1 as the reference, started away from the truth
	// everywhere but the reference, and 0.3 px of noise on every corner.
	rig::reset(4, 12, 0.5);
	rig::scene().pixelNoiseX = rig::scene().pixelNoiseY = 0.3;
	const camera::board::Specification spec = specification();
	Problem problem;
	for (std::size_t c = 0; c < 4; ++c)
	{
		problem.models.push_back(rig::model());
		const math::RigidTransformd truth = rig::scene().referenceFromCamera[c].inverse();
		problem.cameraFromReference.push_back(
			c == 1 ? truth
				   : math::RigidTransformd{math::angleAxis(0.01, math::Vec3d{0, 1, 0}), math::Vec3d{0.01, 0, 0}} * truth);
	}
	problem.reference = 1;
	for (std::size_t g = 0; g < 12; ++g)
	{
		const math::RigidTransformd truth = rig::scene().referenceFromBoard[g];
		problem.referenceFromBody.push_back(math::RigidTransformd{truth.rotation(), truth.translation() + math::Vec3d{0, 0.01, 0}});
		for (std::size_t c = 0; c < 4; ++c)
		{
			const camera::board::DetectionReport d = rig::detection(c, g, rig::frameOf(c, g));
			REQUIRE(d.observation.has_value());
			for (const camera::board::FeatureObservation& f : d.observation->features)
				problem.observations.push_back({std::uint32_t(c), std::uint32_t(g), *spec.cornerPosition(f.id), f.pixel, std::nullopt});
		}
	}
	problem.noise.pixelSigma = 0.3;
	problem.loss.family = LossFamily::None; // a robust loss would shrink the cost compared below
	const Solution solution = refine(problem);
	REQUIRE(solution.status == RefinementStatus::Converged);
	// The reference moved by nothing but its round trip through angle-axis.
	CHECK(rotationBetween(solution.cameraFromReference[1], problem.cameraFromReference[1]) < 1e-12);
	CHECK(math::length(solution.cameraFromReference[1].translation() - problem.cameraFromReference[1].translation()) < 1e-12);
	// The others came 10 mrad and 1 cm back to within the noise: the worst measured at 2.0 mrad and
	// 1.0 mm, from twelve views of 0.3 px noise.
	for (std::size_t c = 0; c < 4; ++c)
	{
		CAPTURE(c);
		const math::RigidTransformd truth = rig::scene().referenceFromCamera[c].inverse();
		CHECK(rotationBetween(solution.cameraFromReference[c], truth) < 0.004);
		CHECK(math::length(solution.cameraFromReference[c].inverse().translation() - truth.inverse().translation()) < 0.002);
	}
	// Whitened by the true noise, the final cost is about half the residual count less the unknowns.
	// Measured: 1.01 of it.
	const double dimensions = 2.0 * double(problem.observations.size()) - 6.0 * (3 + 12);
	CHECK_THAT(solution.finalCost, WithinRel(0.5 * dimensions, 0.15));
}

// The rig at 0.5 m, where the board's tilt shows (syntheticrig.h, reset()), with every pose solved
// turned up to 5 mrad about the board's centre: initialisation starts that far out.
void noisyRig(std::size_t groups, double pixelNoise)
{
	rig::reset(4, groups, 0.5);
	rig::scene().poseNoise = 0.005;
	rig::scene().pixelNoiseX = rig::scene().pixelNoiseY = pixelNoise;
}

// Few resamples: stability is not what these cases are about, and a Debug refinement is slow.
Request quick()
{
	Request request;
	request.resamples = 3;
	return request;
}

TEST_CASE("the refinement converges to the truth from a noisy start, whatever the rig's shape", "[camera][ceres]")
{
	ensureCeres();
	// With exact corners, whatever initialisation's noisy poses left is refined away: measured at
	// 1e-10 for every shape.
	SECTION("connected")
	{
		noisyRig(24, 0.0);
	}
	SECTION("a chain of bridges")
	{
		noisyRig(36, 0.0);
		rig::seenBy(0, 12, {0, 1});
		rig::seenBy(12, 24, {1, 2});
		rig::seenBy(24, 36, {2, 3});
	}
	SECTION("a weak bridge")
	{
		noisyRig(25, 0.0);
		rig::seenBy(0, 12, {0, 1});
		rig::seenBy(12, 13, {1, 2});
		rig::seenBy(13, 25, {2, 3});
	}
	SECTION("every view of one camera flipped")
	{
		noisyRig(24, 0.0);
		for (std::size_t g = 0; g < 24; ++g)
			rig::scene().flipped.insert({3, g});
	}
	const Report report = run(quick());
	const auto [rotation, translation] = worstError(report);
	CHECK(rotation < 1e-8);
	CHECK(translation < 1e-8);
	REQUIRE(report.diagnostics.refinement.has_value());
	CHECK(report.diagnostics.refinement->status == RefinementStatus::Converged);
}

TEST_CASE("a noisy rig registers to within its noise, and its evidence says so", "[camera][ceres]")
{
	ensureCeres();
	noisyRig(24, 0.3);
	const Report report = run(quick());
	// 0.3 px at f = 500 is 0.6 mrad a corner. Measured: the worst camera 0.88 mrad and 0.40 mm out,
	// and a held-out transfer of 0.87 mrad.
	const auto [rotation, translation] = worstError(report);
	CHECK(rotation < 0.002);
	CHECK(translation < 0.001);
	const auto* held = std::get_if<HeldOutEvidence>(&report.heldOut);
	REQUIRE(held != nullptr);
	CHECK(held->rmsAngle > 0.0005);
	CHECK(held->rmsAngle < 0.0015);
	CHECK(report.diagnostics.outliers.empty());
	REQUIRE(std::holds_alternative<ResamplingEvidence>(report.resampling));
}

TEST_CASE("a view from another instant is absorbed by the robust loss and named", "[camera][ceres]")
{
	ensureCeres();
	noisyRig(24, 0.3);
	const math::RigidTransformd& actual = rig::scene().referenceFromBoard[5];
	rig::scene().elsewhere[{2, 5}] =
		math::RigidTransformd{math::angleAxis(0.2, math::Vec3d{0, 0, 1}), math::Vec3d{0.06, 0, 0}} * actual;
	Request request = quick();
	request.heldOutFraction = 0; // so group 5 is fitted

	// Measured, the worst camera's rotation error: 0.68 mrad under Cauchy (the default), 3.4 under
	// Huber, 98 with no robust loss, against 0.88 with no stray view at all.
	SECTION("under the default loss it costs nothing")
	{
		const Report report = run(request);
		CHECK(worstError(report).first < 0.0015);
		REQUIRE(report.diagnostics.outliers.size() == 1);
		CHECK(report.diagnostics.outliers[0].camera.value == "cam02");
		CHECK(report.diagnostics.outliers[0].group == rig::group(5).identity());
	}
	SECTION("with no robust loss it drags the rig")
	{
		request.loss.family = LossFamily::None;
		const Report report = run(request);
		CHECK(worstError(report).first > 0.05);
	}
}

TEST_CASE("corners with a measured covariance are weighted by it", "[camera][ceres]")
{
	ensureCeres();
	// Noise four times larger across than down: weighted by the covariance the detector reports, the
	// noisy direction counts for less, and the residuals come out at one standard deviation.
	noisyRig(24, 0.0);
	rig::scene().pixelNoiseX = 0.8;
	rig::scene().pixelNoiseY = 0.2;
	rig::scene().reportCovariance = true;
	Request request = quick();
	request.resamples = 0;
	request.loss.family = LossFamily::None; // a robust loss would shrink the cost this compares
	const Report weighted = run(request);
	REQUIRE(weighted.diagnostics.refinement.has_value());
	const RefinementSummary& summary = *weighted.diagnostics.refinement;
	// 0.5 r^T C^-1 r summed is about half the residual dimensions less the unknowns: measured 0.998 of it.
	const double dimensions = 2.0 * double(summary.corners) - 6.0 * double(3 + summary.bodies);
	CHECK_THAT(summary.finalCost, WithinRel(0.5 * dimensions, 0.15));
	CHECK(weighted.diagnostics.outliers.empty());

	// The same corners under an isotropic sigma that is wrong in both directions leave a cost far from
	// that: the weighting is what the covariance changed.
	rig::scene().reportCovariance = false;
	request.noise.pixelSigma = 0.2;
	const Report unweighted = run(request);
	CHECK(unweighted.diagnostics.refinement->finalCost > 3 * summary.finalCost);
}

TEST_CASE("a rig of 100 cameras and 4000 capture groups registers", "[camera][ceres][scale]")
{
#ifndef NDEBUG
	SKIP("the scale test runs in Release builds: a Debug refinement is some 65 times slower");
#endif
	ensureCeres();
	// CONTEXT.md's large-rig target. 100 cameras on a ring of 2 m radius, looking in; capture group g
	// shows the board 0.8 m in front of eight neighbouring cameras, starting at camera g mod 100, so
	// each camera sees the board in 320 groups and the ring is closed by its overlaps. 0.3 px of
	// noise on every corner, poses solved up to 5 mrad out.
	constexpr std::size_t kCameras = 100, kGroups = 4000, kSeeing = 8;
	rig::reset(kCameras, kGroups);
	rig::Scene& s = rig::scene();
	const double tau = 6.283185307179586;
	for (std::size_t c = 0; c < kCameras; ++c)
	{
		const double a = tau * double(c) / double(kCameras);
		const math::Vec3d position{2.0 * std::cos(a), -0.05 * double(c % 3), 2.0 * std::sin(a)};
		s.referenceFromCamera[c] = rig::lookingAt(position, math::Vec3d{0.0});
	}
	const math::Vec3d centre = rig::centroid(specification());
	for (std::size_t g = 0; g < kGroups; ++g)
	{
		const std::size_t first = g % kCameras;
		const double a = tau * (double(first) + 3.5) / double(kCameras);
		const double lap = double(g / kCameras) / double(kGroups / kCameras);
		const math::Vec3d inward{-std::cos(a), 0, -std::sin(a)};
		const math::Vec3d down{0, 1, 0};
		const math::Quatd facing = math::quat_cast(math::Mat3d{math::cross(down, inward), down, inward});
		const math::Quatd tilt = math::angleAxis(0.3 * std::sin(tau * 3 * lap), down) *
								 math::angleAxis(0.25 * std::cos(tau * 5 * lap), math::cross(down, inward));
		const math::Vec3d at = 1.2 * math::Vec3d{std::cos(a), 0, std::sin(a)} + math::Vec3d{0, 0.1 * std::sin(tau * 7 * lap), 0};
		const math::Quatd rotation = tilt * facing;
		s.referenceFromBoard[g] = math::RigidTransformd{rotation, at - rotation * centre};
		for (std::size_t c = 0; c < kCameras; ++c)
			s.sees[c][g] = (c + kCameras - first) % kCameras < kSeeing;
	}
	s.poseNoise = 0.005;
	s.pixelNoiseX = s.pixelNoiseY = 0.3;

	const std::vector<method::GroupObservations> groups = rig::groups(true);
	Request request;
	request.resamples = 0;
	const lain::testing::ThreadPool pool;
	const Report report = method::registerCameras(rig::cameras(), groups, specification(), request);
	REQUIRE(report.status == RegistrationStatus::Succeeded);
	// Measured on linux-x86_64, four cores, Release: 19.7 s in all, 16.1 s of it the refinement (9
	// iterations over 768,000 corners), 663 MB at peak. The worst camera 0.94 mrad and 1.3 mm out;
	// held-out transfer 0.81 mrad over 800 groups.
	CHECK(report.diagnostics.groupsUsable == kGroups);
	REQUIRE(report.diagnostics.refinement.has_value());
	CHECK(report.diagnostics.refinement->status == RefinementStatus::Converged);
	const auto [rotation, translation] = worstError(report);
	CHECK(rotation < 0.002);
	CHECK(translation < 0.003);
	const auto* held = std::get_if<HeldOutEvidence>(&report.heldOut);
	REQUIRE(held != nullptr);
	CHECK(held->groups == kGroups / 5);
	CHECK(held->rmsAngle < 0.0015);
}
