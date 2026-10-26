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
#include <lain/camera/comparison.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

	// How far a calibration's model is from the truth: where it sends the truth's rays
	// (camera::compareModels), every 16th pixel centre and the last row and column, and its focal
	// lengths' relative difference. A ray the model cannot project makes it infinitely far.
	//
	// Until 2026-10-26 these cases measured on a grid that stopped short of the right and bottom edges.
	// Reaching them moved the board's model from 1.19 px to 1.44 px at worst, the same on every platform,
	// and on macOS arm64 every other model here by at most 0.19 px at worst and 0.02 px on average,
	// inside every bound. The targetless numbers recorded below are Linux x86-64's, on the old grid.
	ModelComparison errorOf(const calibration::Report& report, const CameraModel& truth)
	{
		REQUIRE(report.model.has_value());
		const ModelComparison c = compareModels(truth, *report.model);
		REQUIRE(c.compared());
		return c;
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

	const ModelComparison boardError = errorOf(board, truth);
	const ModelComparison targetlessError = errorOf(targetless, truth);
	const ModelComparison between = compareModels(*board.model, *targetless.model);
	REQUIRE(between.compared());
	CAPTURE(boardError.toString(), targetlessError.toString(), between.toString(), whitenedOf(targetless));
	// Both Ready on these frames. The board's own residuals are about a tenth of a pixel: held out 0.18
	// mrad against the targetless method's 1.28, which a feature track's localisation sets. Each is
	// judged by its own profile: the board's angle, the targetless residual whitened by its features'
	// stated covariances, 2.15 measured, against a Ready bound of 4.
	CHECK(board.verdict == calibration::Verdict::Ready);
	CHECK(targetless.verdict == calibration::Verdict::Ready);
	CHECK(heldOutOf(board).rmsAngle < 0.0004);
	CHECK(heldOutOf(targetless).rmsAngle < 0.0015);
	CHECK(whitenedOf(targetless) < 4.0);
	// Measured on Linux x86-64: the board 0.071% out in focal length and its ray mapping 1.44 px out at
	// worst (the top-right corner), 0.72 px on average (its principal point 0.6 px off); the targetless model 0.0094%, 0.33 px
	// and 0.23 px; the two 0.062%, 1.19 px and 0.69 px apart. The board is found in 33 of the 48 frames;
	// the targetless method's 40 sampled frames are all placed or held out, 170 of their 780 pairs are
	// refused as seeds, since close views of the board in front of the wall are near one plane, and its
	// second adjustment leaves out 1452 observations the first's result did not explain.
	//
	// The board's numbers are the same on every platform CI runs. The targetless model's are not, since
	// OpenCV's SIFT finds other features elsewhere. Adjusted once, on Windows the same frames gave a
	// model 0.081% out, 1.87 px at worst and 1.23 px on average; on macOS arm64 0.21%, 5.04 px and 3.05
	// px, still Ready: the printed board's minimum recorded below, which the second adjustment is what
	// keeps the fit out of. The targetless bounds hold every platform's model out of it.
	CHECK(boardError.focalDifference < 0.0015);
	CHECK(boardError.worstPixels < 2.5);
	CHECK(boardError.meanPixels < 1.5);
	CHECK(targetlessError.focalDifference < 0.002);
	CHECK(targetlessError.worstPixels < 3.0);
	CHECK(targetlessError.meanPixels < 1.5);
	CHECK(between.focalDifference < 0.003);
	CHECK(between.worstPixels < 3.5);
	CHECK(between.meanPixels < 2.0);
	const calibration::TargetlessDiagnostics& t = targetlessOf(targetless);
	CHECK(t.unplaced.empty());
	CHECK(t.seedPairsRefused > 0);
	CHECK(2 * t.seedPairsRefused < t.seedPairs);
	// Recorded, not pinned (WORK.md, M9 slice 4, sub-slice 10e and after): over six seeds of this footage
	// the board is Ready on all six, 0.009 to 0.071% out in focal length. It was Rejected on two while the
	// stability evidence was a standard deviation, each time for one resample whose focal length was far
	// out (variation 32% and 89%); it is now a robust spread, 0.08 to 0.15%. The targetless model is
	// Ready on all six and 0.21 to 0.55 px out on average. Adjusted once, with every observation, it was
	// Ready on all six too, but on three of them 2.7 to 4.4 px out, its principal point 2 to 4 px off:
	// the printed pattern's repeated corners pulled the fit, the held-out frames' poses absorbed the
	// error, and neither resampling landmarks or frames nor comparing the three starts saw it. Its cost
	// per residual did differ, 1.97 to 2.07 against a right seed's 1.68, and that is the observations
	// the second adjustment leaves out. The next case pins the worst of the three.
}

TEST_CASE("a printed board's repeated corners do not pull the targetless model off", "[camera][calibration][e2e]")
{
#ifndef NDEBUG
	SKIP("calibration end to end runs in Release builds: a Debug build takes seconds to draw each frame");
#endif
	fixture::ensureRegistered();
	const lain::testing::ThreadPool pool;
	// The footage of the case above at seed 5, the worst of the three seeds where the model adjusted
	// once was Ready 2.7 to 4.4 px out: here 4.43 px on average and 7.01 at worst, its principal point
	// 4.2 px off, at a cost per residual of 2.06 against a right seed's 1.68.
	const CameraModel truth = cameraOf(640, 480, 500.0, {8.0, -6.0}, BrownConrady5{-0.1, -0.05, 0.001, -0.002, 0.01});
	const BoardFootage footage = boardFootage(5);
	const calibration::Report report =
		calibration::targetless::calibrate(footageOf(drawn(footage.scene, truth, footage.poses, 5)), {}, {});
	INFO(report.toString());
	REQUIRE(report.status == calibration::CalibrationStatus::Succeeded);
	const ModelComparison error = errorOf(report, truth);
	const math::Vec2d principal{report.model->intrinsics().cx - truth.intrinsics().cx,
								report.model->intrinsics().cy - truth.intrinsics().cy};
	CAPTURE(error.toString(), principal.x, principal.y, targetlessOf(report).pruned, whitenedOf(report));
	// Measured on Linux x86-64: the second adjustment, without the 1615 observations the first's result
	// did not explain, is 0.035% out in focal length, its ray mapping 1.03 px at worst and 0.55 px on
	// average, its principal point 0.53 px off, Ready.
	CHECK(report.verdict == calibration::Verdict::Ready);
	CHECK(targetlessOf(report).pruned > 0);
	CHECK(error.meanPixels < 1.5);
	CHECK(error.worstPixels < 2.5);
	CHECK(math::length(principal) < 1.5);
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
		const ModelComparison error = errorOf(report, truth);
		CAPTURE(error.toString(), heldOutOf(report).rmsAngle, whitenedOf(report));
		// Measured: 0.021% out in focal length, the ray mapping 0.76 px at worst and 0.47 px on average,
		// held out 1.16 mrad, 2.00 whitened. Adjusted once: 0.103%, 1.30 px and 0.77 px.
		CHECK(report.verdict == calibration::Verdict::Ready);
		CHECK(error.focalDifference < 0.002);
		CHECK(error.worstPixels < 2.5);
		CHECK(error.meanPixels < 1.5);
		CHECK(unplacedOf(report) == 0);
	}
	SECTION("a wide lens: f = 250 at 640x480")
	{
		const CameraModel truth = cameraOf(640, 480, 250.0, {1.3, -0.8}, arcLens());
		const calibration::Report report = calibrate(truth);
		INFO(report.toString());
		REQUIRE(report.status == calibration::CalibrationStatus::Succeeded);
		const ModelComparison error = errorOf(report, truth);
		CAPTURE(error.toString(), heldOutOf(report).rmsAngle, whitenedOf(report));
		// Measured: 0.087% out in focal length, the ray mapping 1.43 px at worst and 0.40 px on average,
		// held out 1.84 mrad and 2.35 whitened. A pixel subtends twice the angle at half the focal length,
		// so the held-out angle fails the 1.5 mrad Ready bound the profile had until the held-out residual
		// was judged whitened instead (WORK.md, M9 slice 4, after sub-slice 10e). Adjusted once: 0.123%,
		// 1.83 px and 0.56 px, held out 2.19 mrad and 2.87 whitened.
		CHECK(report.verdict == calibration::Verdict::Ready);
		CHECK(heldOutOf(report).rmsAngle > 0.0015);
		CHECK(whitenedOf(report) < 4.0);
		CHECK(error.focalDifference < 0.0025);
		CHECK(error.worstPixels < 3.5);
		CHECK(error.meanPixels < 1.2);
		CHECK(unplacedOf(report) == 0);
	}
	SECTION("an HD lens: f = 1000 at 1280x720")
	{
		const CameraModel truth = cameraOf(1280, 720, 1000.0, {1.3, -0.8}, arcLens());
		const calibration::Report report = calibrate(truth);
		INFO(report.toString());
		REQUIRE(report.status == calibration::CalibrationStatus::Succeeded);
		const ModelComparison error = errorOf(report, truth);
		CAPTURE(error.toString(), heldOutOf(report).rmsAngle, whitenedOf(report));
		// Measured: 0.325% out in focal length, the ray mapping 5.19 px at worst and 2.08 px on average,
		// held out 0.74 mrad and 1.76 whitened, Ready. Over three seeds 0.19 to 0.33% and 1.45 to 3.56 px
		// on average, the principal point up to 3.1 px off: Ready with an error the held-out frames' poses
		// absorb. This lens sees no board, and its second adjustment narrows the error without removing
		// it: adjusted once, 1.5 to 4.7 px (WORK.md, M9 slice 4, sub-slice 10e). On macOS arm64's
		// features, adjusted once, the same footage gave 0.240%, 5.12 px and 2.43 px, and was Exploratory
		// on its principal point's resampled variation alone, 2.38 mrad against Ready's 2. Either verdict
		// is the model's; that is the only shortfall.
		if (report.verdict != calibration::Verdict::Ready)
		{
			CHECK(report.verdict == calibration::Verdict::Exploratory);
			REQUIRE(report.fitnessNotes.size() == 1);
			CHECK_THAT(report.fitnessNotes.front(), ContainsSubstring("principal point variation"));
		}
		CHECK(error.focalDifference < 0.004);
		CHECK(error.worstPixels < 8.0);
		CHECK(error.meanPixels < 4.0);
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
	const ModelComparison error = errorOf(report, truth);
	CAPTURE(error.toString(), heldOutOf(report).rmsAngle, whitenedOf(report));
	// Measured on Linux x86-64: 0.070% out in focal length, the ray mapping 1.56 px at worst and 1.25 px
	// on average, held out 0.59 mrad and 1.58 whitened, Ready; adjusted once, 0.094%, 1.26 px and 0.90
	// px. On macOS arm64's features, adjusted once: 0.113%, 3.11 px and 2.70 px, held out 1.79 whitened,
	// still Ready, a model a few pixels out that the verdict does not see (WORK.md, M9 slice 4, after
	// sub-slice 10e, item 4); the bounds hold both.
	CHECK(report.verdict == calibration::Verdict::Ready);
	CHECK(error.focalDifference < 0.002);
	CHECK(error.worstPixels < 4.5);
	CHECK(error.meanPixels < 3.5);
}
