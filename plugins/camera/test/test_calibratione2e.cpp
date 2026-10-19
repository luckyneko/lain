// Camera calibration end to end through both real backends, on footage drawn in plain C++
// (texturedscene.h), so every model is judged against its truth: by its ray mapping over the image,
// never by its coefficients. OpenCV detects the board and extracts, matches and verifies features;
// Ceres refines. M9 slice 4, sub-slice 10e:
// - one camera calibrated both ways from the same frames, each model compared with the truth and
//   with the other;
// - the default 40 sampled frames calibrating a moving camera through a normal, a wide and an HD
//   lens, which sub-slice 10c's three starts made work where a two-view focal sweep had failed;
// - a rendered pan refused before any start (10d's homography test);
// - a strongly distorted lens leaving no frame unplaced.
//
// Every case draws frames at 640x480 or more, so these run in Release only: a Debug build draws a frame
// in seconds. Every bound is a measurement, written beside its check.

#include "registration.h"
#include "testboard.h"
#include "texturedscene.h"

#include <lain/camera/board/rendering.h>
#include <lain/camera/calibration/board.h>
#include <lain/camera/calibration/targetless.h>
#include <lain/camera/projection.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <variant>
#include <vector>

using namespace lain;
using namespace lain::camera;
using Catch::Matchers::ContainsSubstring;

namespace
{
	constexpr double kTau = 6.283185307179586;

	// A camera of `width` by `height` with focal lengths f and 1.004 f, its principal point `offset`
	// from the image's centre, seen through `distortion`.
	CameraModel cameraOf(std::uint32_t width, std::uint32_t height, double f, math::Vec2d offset, const Distortion& distortion)
	{
		CameraModelParameters p;
		p.image = {width, height};
		p.intrinsics = {f, 1.004 * f, 0.5 * double(width) - 0.5 + offset.x, 0.5 * double(height) - 0.5 + offset.y};
		p.distortion = distortion;
		const ModelResult result = CameraModel::create(p);
		REQUIRE(result.model.has_value());
		return *result.model;
	}

	// How far a model is from another: the larger of its focal lengths' relative errors, and the ray
	// mapping's worst and mean, every 16th pixel's ray through `truth` projected through `estimate`, in
	// pixels. A pixel `estimate` cannot project counts as infinitely far.
	struct ModelError
	{
		double focal = 0;
		double worst = 0;
		double mean = 0;
	};
	ModelError errorOf(const CameraModel& estimate, const CameraModel& truth)
	{
		ModelError out;
		out.focal = std::max(std::abs(estimate.intrinsics().fx / truth.intrinsics().fx - 1),
							 std::abs(estimate.intrinsics().fy / truth.intrinsics().fy - 1));
		double sum = 0;
		std::size_t count = 0;
		for (double v = 0; v < double(truth.image().height); v += 16)
		{
			for (double u = 0; u < double(truth.image().width); u += 16)
			{
				const Unprojection<double> ray = unproject(truth, u, v);
				REQUIRE(ray.ok());
				const Projection<double> p = project(estimate, ray.x, ray.y, ray.z);
				const double d = p.ok() ? std::hypot(p.u - u, p.v - v) : std::numeric_limits<double>::infinity();
				out.worst = std::max(out.worst, d);
				sum += d;
				++count;
			}
		}
		out.mean = sum / double(count);
		return out;
	}

	ModelError errorOf(const calibration::Report& report, const CameraModel& truth)
	{
		REQUIRE(report.model.has_value());
		return errorOf(*report.model, truth);
	}

	capture::CameraFootage footageOf(std::shared_ptr<synthetic::RenderedSource> source)
	{
		return capture::CameraFootage{capture::CameraIdentity{"moving"}, media::FrameSequence::over(std::move(source))};
	}

	const calibration::TargetlessDiagnostics& targetlessOf(const calibration::Report& report)
	{
		return std::get<calibration::TargetlessDiagnostics>(report.diagnostics.method);
	}

	const calibration::HeldOutEvidence& heldOutOf(const calibration::Report& report)
	{
		REQUIRE(std::holds_alternative<calibration::HeldOutEvidence>(report.heldOut));
		return std::get<calibration::HeldOutEvidence>(report.heldOut);
	}

	// The held-out residual whitened by each feature's stated covariance, which a targetless report
	// always measures: every extracted observation states one.
	double whitenedOf(const calibration::Report& report)
	{
		const calibration::HeldOutEvidence& heldOut = heldOutOf(report);
		REQUIRE(heldOut.rmsWhitened.has_value());
		return *heldOut.rmsWhitened;
	}

	// The frames neither placed nor held out.
	std::size_t unplacedOf(const calibration::Report& report)
	{
		return targetlessOf(report).unplaced.size();
	}

	// One camera's footage of the default scene, `poses` a frame, through `truth`, with pixel noise of
	// one grey level; its texture and noise both from `seed`. Drawn before it is returned, on the pool.
	std::shared_ptr<synthetic::RenderedSource> drawn(synthetic::Scene scene, const CameraModel& truth,
													 std::vector<math::RigidTransformd> poses, std::uint64_t seed)
	{
		scene.noise = 1.0;
		auto source = std::make_shared<synthetic::RenderedSource>("moving", std::move(scene), truth, std::move(poses), seed);
		source->drawAll();
		return source;
	}

	// The footage both methods calibrate from (the probe that planned sub-slice 10 drew it): a 7x5 board
	// of 120 mm squares standing square to the scene 1.2 m in front of the wall, and a camera that
	// walks sub-slice 8's arc for 24 frames, turns towards the board over 6 more, then takes 18 close
	// views of it from 0.8 to 1.2 m, swinging it up to 0.35 m off centre so its corners reach the
	// image's corners. 48 frames.
	struct BoardFootage
	{
		board::Specification spec;
		std::vector<math::RigidTransformd> poses;
		synthetic::Scene scene;
	};

	BoardFootage boardFootage(std::uint64_t seed)
	{
		BoardFootage out{camera::testing::specification(camera::testing::pattern(), 120.0), {}, synthetic::defaultScene(seed)};
		const board::RenderResult rendered = board::render(out.spec.pattern(), board::RenderRequest{120, 30});
		REQUIRE(rendered.rendering.has_value());
		const double square = out.spec.instance().squareLength.value.metres();
		const math::Vec3d corner{0.6, -0.25, 1.2};
		const math::Vec3d centre = corner + math::Vec3d{3.5 * square, 2.5 * square, 0.0};

		out.poses = synthetic::arcPath(24);
		std::vector<math::RigidTransformd> close;
		for (std::size_t k = 0; k < 18; ++k)
		{
			const double s = double(k) / 18.0;
			const double azimuth = 0.5 * std::sin(kTau * s), elevation = 0.3 * std::cos(2 * kTau * s);
			const math::Vec3d direction{std::sin(azimuth) * std::cos(elevation), std::sin(elevation),
										-std::cos(azimuth) * std::cos(elevation)};
			const math::Vec3d at = centre + math::Vec3d{0.35 * std::cos(3 * kTau * s), 0.25 * std::sin(3 * kTau * s), 0.0};
			const math::Vec3d position = centre + (1.0 + 0.2 * std::sin(2 * kTau * s + 1)) * direction;
			close.push_back(synthetic::lookingAt(position, at) *
							math::RigidTransformd{math::angleAxis(0.15 * std::sin(kTau * 1.5 * s), math::Vec3d{0.0, 0.0, 1.0}),
												  math::Vec3d{0.0}});
		}
		// From the arc's last pose to the first close view along a line, looking from the box to the board.
		const math::Vec3d from = out.poses.back().translation(), to = close.front().translation();
		const math::Vec3d box{-0.2, 0.35, 0.1};
		for (std::size_t k = 1; k <= 6; ++k)
		{
			const double s = double(k) / 7.0;
			out.poses.push_back(synthetic::lookingAt(from + s * (to - from), box + s * (centre - box)));
		}
		out.poses.insert(out.poses.end(), close.begin(), close.end());
		out.scene.board = synthetic::boardSurface(std::make_shared<const board::Rendering>(*rendered.rendering), square,
												  std::vector<math::RigidTransformd>(out.poses.size(),
																					 math::RigidTransformd{math::Quatd{1, 0, 0, 0}, corner}));
		return out;
	}

	// A camera that only turns: where arcCamera(0) stands, panning 0.6 rad across the default scene and
	// tilting a little as it goes, so the turn is not about one axis.
	std::vector<math::RigidTransformd> panPath(std::size_t frames)
	{
		const math::Vec3d position = synthetic::arcCamera(0.0).translation();
		const math::RigidTransformd base = synthetic::lookingAt(position, {-0.2, 0.35, 0.1});
		std::vector<math::RigidTransformd> path;
		for (std::size_t k = 0; k < frames; ++k)
		{
			const double s = double(k) / double(frames - 1) - 0.5;
			const math::Quatd turn = math::angleAxis(0.6 * s, math::Vec3d{0, 1, 0}) *
									 math::angleAxis(0.1 * std::sin(kTau * s), math::Vec3d{1, 0, 0});
			path.push_back(math::RigidTransformd{base.rotation() * turn, position});
		}
		return path;
	}

	// sub-slice 8's lens: every coefficient distinct.
	BrownConrady5 arcLens()
	{
		return {0.1, -0.05, 0.001, -0.002, 0.01};
	}
} // namespace

TEST_CASE("one camera calibrated with a board and without one agrees with itself and its truth",
		  "[camera][calibration][e2e]")
{
#ifndef NDEBUG
	SKIP("calibration end to end runs in Release builds: a Debug build takes seconds to draw each frame");
#endif
	fixture::ensureRegistered();
	const lain::testing::ThreadPool pool;
	const CameraModel truth = cameraOf(640, 480, 500.0, {8.0, -6.0}, BrownConrady5{-0.1, -0.05, 0.001, -0.002, 0.01});
	const BoardFootage footage = boardFootage(3);
	const auto source = drawn(footage.scene, truth, footage.poses, 3);

	const calibration::Report board =
		calibration::board::calibrate(media::FrameSequence::over(source), footage.spec, board::DetectionRequest{}, {});
	INFO("board: " << board.toString());
	const calibration::Report targetless = calibration::targetless::calibrate(footageOf(source), {}, {});
	INFO("targetless: " << targetless.toString());
	REQUIRE(board.status == calibration::CalibrationStatus::Succeeded);
	REQUIRE(targetless.status == calibration::CalibrationStatus::Succeeded);

	const ModelError boardError = errorOf(board, truth);
	const ModelError targetlessError = errorOf(targetless, truth);
	const ModelError between = errorOf(*targetless.model, *board.model);
	CAPTURE(boardError.focal, boardError.worst, boardError.mean, targetlessError.focal, targetlessError.worst,
			targetlessError.mean, between.focal, between.worst, between.mean, whitenedOf(targetless));
	// Both Ready on these frames. The board's own residuals are about a tenth of a pixel: held out 0.18
	// mrad against the targetless method's 1.22, which a feature track's localisation sets. Each is
	// judged by its own profile: the board's angle, the targetless residual whitened by its features'
	// stated covariances, 2.16 measured, against a Ready bound of 4.
	CHECK(board.verdict == calibration::Verdict::Ready);
	CHECK(targetless.verdict == calibration::Verdict::Ready);
	CHECK(heldOutOf(board).rmsAngle < 0.0004);
	CHECK(heldOutOf(targetless).rmsAngle < 0.0015);
	CHECK(whitenedOf(targetless) < 4.0);
	// Measured: the board 0.071% out in focal length and its ray mapping 1.19 px out at worst, 0.72 px on
	// average (its principal point 0.6 px off); the targetless model 0.033%, 0.56 px and 0.30 px; the two
	// 0.090%, 1.65 px and 0.93 px apart. The board is found in 33 of the 48 frames; the targetless
	// method's 40 sampled frames are all placed or held out, and 170 of their 780 pairs are refused as
	// seeds, since close views of the board in front of the wall are near one plane.
	CHECK(boardError.focal < 0.0015);
	CHECK(boardError.worst < 2.5);
	CHECK(boardError.mean < 1.5);
	CHECK(targetlessError.focal < 0.001);
	CHECK(targetlessError.worst < 1.2);
	CHECK(targetlessError.mean < 0.6);
	CHECK(between.focal < 0.002);
	CHECK(between.worst < 3.0);
	CHECK(between.mean < 1.8);
	const calibration::TargetlessDiagnostics& t = targetlessOf(targetless);
	CHECK(t.unplaced.empty());
	CHECK(t.seedPairsRefused > 0);
	CHECK(2 * t.seedPairsRefused < t.seedPairs);
	// Recorded, not pinned (WORK.md, M9 slice 4, sub-slice 10e and after): over six seeds of this footage
	// the board is Ready on all six, 0.009 to 0.071% out in focal length. It was Rejected on two while the
	// stability evidence was a standard deviation, each time for one resample whose focal length was far
	// out (variation 32% and 89%); it is now a robust spread, 0.08 to 0.15%. The targetless model is
	// Ready on all six, but on three of them 2.7 to 4.4 px out on average, its principal point 2 to 4 px
	// off, with a resampled principal-point variation of 0.29 to 0.68 mrad against 0.025 to 0.43 on the
	// other three. Without the board the same path stays within 0.3 to 1.6 px: the printed pattern's
	// repeated corners mislead the fit, and resampling from the result cannot see a wrong minimum.
}

TEST_CASE("the default sampled frames calibrate a moving camera through a normal, a wide and an HD lens",
		  "[camera][calibration][e2e]")
{
#ifndef NDEBUG
	SKIP("calibration end to end runs in Release builds: a Debug build takes seconds to draw each frame");
#endif
	fixture::ensureRegistered();
	const lain::testing::ThreadPool pool;
	// 40 frames along sub-slice 8's arc, the default 40 of them sampled, at seed 2.
	const auto calibrate = [](const CameraModel& truth)
	{
		return calibration::targetless::calibrate(footageOf(drawn(synthetic::defaultScene(2), truth, synthetic::arcPath(40), 2)),
												  {}, {});
	};
	SECTION("a normal lens: f = 500 at 640x480")
	{
		const CameraModel truth = cameraOf(640, 480, 500.0, {1.3, -0.8}, arcLens());
		const calibration::Report report = calibrate(truth);
		INFO(report.toString());
		REQUIRE(report.status == calibration::CalibrationStatus::Succeeded);
		const ModelError error = errorOf(report, truth);
		CAPTURE(error.focal, error.worst, error.mean, heldOutOf(report).rmsAngle, whitenedOf(report));
		// Measured: 0.103% out in focal length, the ray mapping 1.30 px at worst and 0.77 px on average,
		// held out 1.22 mrad, 2.35 whitened.
		CHECK(report.verdict == calibration::Verdict::Ready);
		CHECK(error.focal < 0.002);
		CHECK(error.worst < 2.5);
		CHECK(error.mean < 1.5);
		CHECK(unplacedOf(report) == 0);
	}
	SECTION("a wide lens: f = 250 at 640x480")
	{
		const CameraModel truth = cameraOf(640, 480, 250.0, {1.3, -0.8}, arcLens());
		const calibration::Report report = calibrate(truth);
		INFO(report.toString());
		REQUIRE(report.status == calibration::CalibrationStatus::Succeeded);
		const ModelError error = errorOf(report, truth);
		CAPTURE(error.focal, error.worst, error.mean, heldOutOf(report).rmsAngle, whitenedOf(report));
		// Measured: 0.123% out in focal length, the ray mapping 1.83 px at worst and 0.56 px on average,
		// held out 0.74 px, about the normal lens's 0.61, and 2.87 whitened. A pixel subtends twice the
		// angle at half the focal length, so as an angle that is 2.19 mrad, which failed the 1.5 mrad
		// Ready bound the profile had until the held-out residual was judged whitened instead (WORK.md,
		// M9 slice 4, after sub-slice 10e).
		CHECK(report.verdict == calibration::Verdict::Ready);
		CHECK(heldOutOf(report).rmsAngle > 0.0015);
		CHECK(whitenedOf(report) < 4.0);
		CHECK(error.focal < 0.0025);
		CHECK(error.worst < 3.5);
		CHECK(error.mean < 1.2);
		CHECK(unplacedOf(report) == 0);
	}
	SECTION("an HD lens: f = 1000 at 1280x720")
	{
		const CameraModel truth = cameraOf(1280, 720, 1000.0, {1.3, -0.8}, arcLens());
		const calibration::Report report = calibrate(truth);
		INFO(report.toString());
		REQUIRE(report.status == calibration::CalibrationStatus::Succeeded);
		const ModelError error = errorOf(report, truth);
		CAPTURE(error.focal, error.worst, error.mean, heldOutOf(report).rmsAngle, whitenedOf(report));
		// Measured: 0.243% out in focal length, the ray mapping 5.05 px at worst and 2.33 px on average,
		// held out 0.78 mrad and 1.96 whitened, Ready. Over three seeds 0.20 to 0.24% and 1.5 to 4.7 px on average, the
		// principal point up to 3.9 px off and its resampled variation 1.7 to 2.0 mrad: Ready with an error
		// the held-out frames' poses absorb (WORK.md, M9 slice 4, sub-slice 10e).
		CHECK(report.verdict == calibration::Verdict::Ready);
		CHECK(error.focal < 0.004);
		CHECK(error.worst < 8.0);
		CHECK(error.mean < 4.0);
		CHECK(unplacedOf(report) == 0);
	}
}

TEST_CASE("a rendered pan is refused before any start", "[camera][calibration][e2e]")
{
#ifndef NDEBUG
	SKIP("calibration end to end runs in Release builds: a Debug build takes seconds to draw each frame");
#endif
	fixture::ensureRegistered();
	const lain::testing::ThreadPool pool;
	const CameraModel truth = cameraOf(640, 480, 500.0, {1.3, -0.8}, BrownConrady5{});
	calibration::targetless::Settings settings;
	settings.extraction.samples = 12;
	const calibration::Report report =
		calibration::targetless::calibrate(footageOf(drawn(synthetic::defaultScene(), truth, panPath(24), 1)), settings, {});
	INFO(report.toString());
	REQUIRE(report.status == calibration::CalibrationStatus::Failed);
	REQUIRE(report.failures.size() == 1);
	CHECK(report.failures.front().failure == calibration::Failure::InitialisationFailed);
	CHECK_THAT(report.failures.front().detail, ContainsSubstring("a homography explains"));
	CHECK_THAT(report.failures.front().detail, ContainsSubstring("turned without moving"));
	const calibration::TargetlessDiagnostics& t = targetlessOf(report);
	// Every pair of the twelve sampled frames shares enough tracks to seed, and each is explained.
	CHECK(t.seedPairs == 66);
	CHECK(t.seedPairsRefused == 66);
	CHECK(t.sweep.empty());
	CHECK(t.seed.empty());
}

TEST_CASE("a strongly distorted lens leaves no frame unplaced", "[camera][calibration][e2e]")
{
#ifndef NDEBUG
	SKIP("calibration end to end runs in Release builds: a Debug build takes seconds to draw each frame");
#endif
	fixture::ensureRegistered();
	const lain::testing::ThreadPool pool;
	// Sub-slice 8's strongest lens: k1 = -0.25 alone at 1920x1080 with f = 1500, which moves the image's
	// corner 317 px. 24 frames along the arc, 20 of them sampled.
	const CameraModel truth = cameraOf(1920, 1080, 1500.0, {1.3, -0.8}, BrownConrady5{-0.25, 0.0, 0.0, 0.0, 0.0});
	calibration::targetless::Settings settings;
	settings.extraction.samples = 20;
	const calibration::Report report =
		calibration::targetless::calibrate(footageOf(drawn(synthetic::defaultScene(1), truth, synthetic::arcPath(24), 1)), settings, {});
	INFO(report.toString());
	REQUIRE(report.status == calibration::CalibrationStatus::Succeeded);
	// Every sampled frame is placed or held out: the starting model's domain, unbounded and undistorted,
	// reaches every frame, so no second placement round under the refined model is owed.
	const calibration::TargetlessDiagnostics& t = targetlessOf(report);
	CHECK(t.unplaced.empty());
	CHECK(t.placement.size() + report.diagnostics.heldOut.size() == t.frames.size());
	const ModelError error = errorOf(report, truth);
	CAPTURE(error.focal, error.worst, error.mean, heldOutOf(report).rmsAngle, whitenedOf(report));
	// Measured: 0.094% out in focal length, the ray mapping 1.26 px at worst and 0.90 px on average,
	// held out 0.61 mrad and 1.80 whitened, Ready.
	CHECK(report.verdict == calibration::Verdict::Ready);
	CHECK(error.focal < 0.002);
	CHECK(error.worst < 2.5);
	CHECK(error.mean < 1.8);
}
