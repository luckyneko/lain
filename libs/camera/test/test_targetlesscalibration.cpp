// Targetless calibration (calibration::targetless) over stand-ins answering from a known moving camera
// (syntheticscene.h) and a refiner that hands back its start (passthroughrefiner.h): the focal sweep's
// starts and which of them wins, the starting model under each imported-model policy, the held-out frames, which frames are placed,
// the refusals, and that nothing depends on the order the evidence arrives in or the execution policy.
// The refiner refines nothing, so what a model comes out as is where the method started it; the
// Ceres refiner's recovery is plugins/camera/ceres's to show. Every measured number is written beside
// its check.

#include "passthroughrefiner.h"
#include "syntheticscene.h"
#include "targetlesscalibrationtext.h"

#include <lain/camera/calibration/targetless.h>
#include <lain/camera/feature/sequenceextraction.h>
#include <lain/meta/enums.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::calibration;
namespace scene = lain::camera::testing::scene;
namespace standin = lain::camera::testing;
using Catch::Matchers::ContainsSubstring;
using lain::camera::testing::targetlesstext::name;
using lain::camera::testing::targetlesstext::requireSameReport;

namespace
{
	// A pinhole at 640x480 with fx = fy = `f` and its principal point at the image's centre: the shape
	// every start of the focal sweep has, so one at a start's focal length is that start.
	CameraModel pinhole(double f)
	{
		CameraModelParameters p;
		p.image = {640, 480};
		p.intrinsics = {f, f, 319.5, 239.5};
		p.distortion = NoDistortion{};
		return *CameraModel::create(p).model;
	}

	// The equidistant fisheye a Kannala-Brandt 4 model is with every coefficient zero, shaped as pinhole().
	CameraModel equidistant(double f)
	{
		CameraModelParameters p;
		p.image = {640, 480};
		p.intrinsics = {f, f, 319.5, 239.5};
		p.distortion = KannalaBrandt4{};
		return *CameraModel::create(p).model;
	}

	// A wandering camera (scene::resetWandering) seeing `landmarks` through `camera`, the stand-ins
	// registered, the refiner's log cleared, and the refiner saying it estimates every model.
	void setUp(std::size_t frames = 12, std::size_t landmarks = 120, const CameraModel& camera = pinhole(500))
	{
		scene::registerStandIns();
		standin::registerPassThroughRefiner();
		scene::resetWandering(frames, landmarks, 1, camera);
		std::lock_guard<std::mutex> lock(standin::refinerScript().mutex);
		standin::refinerScript().asked.clear();
		standin::refinerScript().estimates = {DistortionModel::None, DistortionModel::BrownConrady5,
											  DistortionModel::KannalaBrandt4};
		standin::refinerScript().finalCost = nullptr;
	}

	std::vector<standin::RefinerScript::Asked> asked()
	{
		std::lock_guard<std::mutex> lock(standin::refinerScript().mutex);
		return standin::refinerScript().asked;
	}

	Request pinholeRequest()
	{
		Request request;
		request.model = DistortionModel::None;
		return request;
	}

	Report run(const feature::TrackSet& tracks, const Request& request = pinholeRequest(), const targetless::Settings& settings = {})
	{
		return targetless::calibrate(tracks, settings, request);
	}

	const TargetlessDiagnostics& targetlessOf(const Report& report)
	{
		return std::get<TargetlessDiagnostics>(report.diagnostics.method);
	}

	bool failedWith(const Report& report, Failure failure)
	{
		return report.status == CalibrationStatus::Failed && !report.failures.empty() && report.failures.front().failure == failure;
	}

	// The frame `k` of the path can see only the landmarks in `seen`, as far as the truth goes.
	void onlySees(std::size_t k, const std::set<std::size_t>& seen)
	{
		for (std::size_t l = 0; l < scene::scene().landmarks.size(); ++l)
		{
			if (seen.count(l) == 0)
				scene::scene().landmarks[l].hidden.insert({0, k});
		}
	}

	// The track set with its views and its tracks listed backwards, each track's observations too, so
	// nothing about the evidence but its order changes.
	feature::TrackSet reversed(const feature::TrackSet& set)
	{
		feature::TrackSet out = set;
		std::reverse(out.views.begin(), out.views.end());
		const std::uint32_t last = std::uint32_t(set.views.size() - 1);
		for (feature::Track& t : out.tracks)
		{
			for (feature::SceneObservation& o : t.observations)
				o.view = last - o.view;
			std::reverse(t.observations.begin(), t.observations.end());
		}
		std::reverse(out.tracks.begin(), out.tracks.end());
		return out;
	}

	// The sweep's step: its starts are log-spaced, so each is this factor longer than the one before.
	double sweepStep(const TargetlessDiagnostics& t)
	{
		return t.sweep[1].focalLength / t.sweep[0].focalLength;
	}

	// Why a start of the sweep ended in no model, or nothing when it ended in one.
	const FailureReason* failureOf(const SweepStart& start)
	{
		return std::get_if<FailureReason>(&start.outcome);
	}
} // namespace

TEST_CASE("the focal sweep starts three times, and the start nearest the truth is the result",
		  "[camera][calibration][targetless]")
{
	// The pinhole's range at 640x480 runs from 184.75 (120 degrees across) to 1443.4 (25 degrees); its
	// starts are the centres of the range's geometric thirds, 260.25, 516.41 and 1024.69, each 1.9843
	// times the one before. The fisheye's, through its own neutral projection, are 396.88, 669.49 and
	// 1129.33, 1.6869 apart. The stand-in geometry proposes only true poses, so only a start near the truth poses a
	// seed: the others fail, and are reported without ending the calibration.
	struct Case
	{
		CameraModel truth;
		DistortionModel model;
		double first, step;
		std::uint32_t nearest;
	};
	for (const Case& c : {Case{pinhole(250), DistortionModel::None, 260.25, 1.98427, 0},
						  Case{pinhole(500), DistortionModel::None, 260.25, 1.98427, 1},
						  Case{pinhole(1000), DistortionModel::None, 260.25, 1.98427, 2},
						  Case{equidistant(650), DistortionModel::KannalaBrandt4, 396.88, 1.68687, 1}})
	{
		const double f = c.truth.intrinsics().fx;
		DYNAMIC_SECTION(std::string(displayName(c.truth.distortion())) << " at " << f)
		{
			setUp(12, 120, c.truth);
			Request request;
			request.model = c.model;
			const Report report = run(scene::sequenceTrackSetOf(), request);
			INFO(report.toString());
			REQUIRE(report.status == CalibrationStatus::Succeeded);
			const TargetlessDiagnostics& t = targetlessOf(report);
			REQUIRE(t.sweep.size() == 3);
			CHECK(std::abs(t.sweep[0].focalLength - c.first) < 0.01);
			CHECK(std::abs(sweepStep(t) - c.step) < 1e-4);
			CHECK(std::abs(t.sweep[2].focalLength / t.sweep[1].focalLength - c.step) < 1e-4);
			CHECK(t.swept == std::optional<std::uint32_t>{c.nearest});
			for (std::uint32_t i = 0; i < 3; ++i)
			{
				CAPTURE(i);
				if (i == c.nearest)
					CHECK(std::holds_alternative<double>(t.sweep[i].outcome));
				else
					CHECK(failureOf(t.sweep[i]) != nullptr);
			}
			CHECK(report.seededFields.empty());
			REQUIRE(report.model.has_value());
			CHECK(report.model->intrinsics().fx == t.sweep[c.nearest].focalLength);
			CHECK(report.model->intrinsics().cx == 319.5);
			CHECK(modelOf(report.model->distortion()) == c.model);
		}
	}
}

TEST_CASE("the start whose adjustment ends with the lowest cost per residual wins, ties to the shorter",
		  "[camera][calibration][targetless]")
{
	// A camera that only slides explains its rays under any focal length, so every start reconstructs
	// and adjusts; the refiner stand-in reports the cost a case scripts, here the distance of a
	// problem's focal length from a chosen one times its residuals.
	setUp();
	scene::resetSliding(12, 120, 1, pinhole(500));
	const feature::TrackSet set = scene::sequenceTrackSetOf();
	const auto costFrom = [](double best)
	{
		return [best](const refinement::Problem& problem)
		{ return std::abs(std::log(problem.models.front().intrinsics().fx / best)) * double(problem.landmarkObservations.size()); };
	};
	for (const auto& [best, winner] : {std::pair<double, std::uint32_t>{250.0, 0}, {500.0, 1}, {1000.0, 2}})
	{
		CAPTURE(best);
		{
			std::lock_guard<std::mutex> lock(standin::refinerScript().mutex);
			standin::refinerScript().finalCost = costFrom(best);
		}
		const Report report = run(set);
		INFO(report.toString());
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		const TargetlessDiagnostics& t = targetlessOf(report);
		REQUIRE(t.sweep.size() == 3);
		for (std::uint32_t i = 0; i < 3; ++i)
		{
			CAPTURE(i);
			REQUIRE(std::holds_alternative<double>(t.sweep[i].outcome));
			CHECK(std::abs(std::get<double>(t.sweep[i].outcome) - std::abs(std::log(t.sweep[i].focalLength / best))) < 1e-12);
		}
		CHECK(t.swept == std::optional<std::uint32_t>{winner});
		CHECK(report.model->intrinsics().fx == t.sweep[winner].focalLength);
	}
	{
		std::lock_guard<std::mutex> lock(standin::refinerScript().mutex);
		standin::refinerScript().finalCost = nullptr; // every cost zero
	}
	CHECK(targetlessOf(run(set)).swept == std::optional<std::uint32_t>{0});
}

TEST_CASE("each start poses at 4 pixels of its own focal length", "[camera][calibration][targetless]")
{
	setUp();
	scene::scene().relativeAngles.clear();
	const Report report = run(scene::sequenceTrackSetOf());
	const TargetlessDiagnostics& t = targetlessOf(report);
	REQUIRE(t.sweep.size() == 3);
	// Every start, failing or not, poses its seed candidates at 4 px of its focal length.
	std::set<double> handed(scene::scene().relativeAngles.begin(), scene::scene().relativeAngles.end());
	for (const SweepStart& tried : t.sweep)
		CHECK(handed.count(4.0 / tried.focalLength) == 1);
}

TEST_CASE("an imported model starts the reconstruction instead of the sweep", "[camera][calibration][targetless]")
{
	setUp();
	const feature::TrackSet set = scene::sequenceTrackSetOf();
	SECTION("Initial, the same model: every field seeded")
	{
		Request request = pinholeRequest();
		request.imported = pinhole(500);
		request.importedPolicy = ImportedModelPolicy::Initial;
		const Report report = run(set, request);
		INFO(report.toString());
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		CHECK(targetlessOf(report).sweep.empty());
		CHECK_FALSE(targetlessOf(report).swept.has_value());
		CHECK(report.seededFields == std::vector<std::string>{"fx", "fy", "cx", "cy"});
		CHECK(report.model->intrinsics().fx == 500);
		const std::vector<standin::RefinerScript::Asked> calls = asked();
		REQUIRE_FALSE(calls.empty());
		REQUIRE(calls[0].modelFreedom.size() == 1);
		CHECK(calls[0].modelFreedom[0].focalLength);
		CHECK(calls[0].modelFreedom[0].coefficients);
		CHECK(calls[0].modelFreedom[0].principalPoint);
		// Exact tracks, the true model and the true poses: every held-out frame predicted exactly.
		const HeldOutEvidence& held = std::get<HeldOutEvidence>(report.heldOut);
		CHECK(held.views == 2);
		CHECK(held.viewsWithoutPose == 0);
		CHECK(held.rmsAngle < 1e-9);
		CHECK(held.outliers == 0);
	}
	SECTION("Initial, another model: the pinhole part seeded, the distortion neutral")
	{
		Request request;
		request.model = DistortionModel::BrownConrady5;
		request.imported = pinhole(500);
		request.importedPolicy = ImportedModelPolicy::Initial;
		const Report report = run(set, request);
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		CHECK(targetlessOf(report).sweep.empty());
		CHECK(report.seededFields == std::vector<std::string>{"fx", "fy", "cx", "cy"});
		CHECK(modelOf(report.model->distortion()) == DistortionModel::BrownConrady5);
		CHECK(coefficients(report.model->distortion()) == std::vector<double>(5, 0.0));
	}
	SECTION("HoldAndValidate: nothing freed, at most Exploratory, no resampling")
	{
		setUp(14, 120); // so six frames can be held out to validate on: Exploratory needs six views
		{
			std::lock_guard<std::mutex> lock(standin::refinerScript().mutex);
			standin::refinerScript().estimates.clear(); // a held model needs no estimator
		}
		Request request = pinholeRequest();
		request.imported = pinhole(500);
		request.importedPolicy = ImportedModelPolicy::HoldAndValidate;
		request.heldOutFraction = 0.5; // every second frame of 14 but the anchor's frame 1: six validate it
		const Report report = run(scene::sequenceTrackSetOf(), request);
		INFO(report.toString());
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		CHECK(targetlessOf(report).sweep.empty());
		CHECK(report.seededFields.empty());
		CHECK(report.model->intrinsics().fx == 500);
		CHECK(report.reproducibility.estimator.backend.empty());
		CHECK(std::get<TargetlessRecord>(report.reproducibility.method).refiner.backend == "passthrough");
		const std::vector<standin::RefinerScript::Asked> calls = asked();
		REQUIRE(calls.size() == 1);
		CHECK(calls[0].modelFreedom.empty());
		CHECK(std::holds_alternative<Unavailable>(report.resampling));
		CHECK(report.diagnostics.selected.empty());
		CHECK(std::get<HeldOutEvidence>(report.heldOut).views == 6);
		CHECK(report.verdict == Verdict::Exploratory);
	}
}

TEST_CASE("held-out frames are every fifth in capture order, and never reach a refinement", "[camera][calibration][targetless]")
{
	setUp();
	const feature::TrackSet set = scene::sequenceTrackSetOf();
	Request request = pinholeRequest();
	request.resamples = 4;
	const Report report = run(set, request);
	INFO(report.toString());
	REQUIRE(report.status == CalibrationStatus::Succeeded);
	CHECK(report.diagnostics.heldOut == std::vector<std::uint32_t>{2, 7});
	CHECK(report.diagnostics.viewsHeldOut == 2);
	const TargetlessDiagnostics& t = targetlessOf(report);
	for (const std::uint32_t f : t.placement)
		CHECK((f != 2 && f != 7));
	CHECK(report.diagnostics.selected == std::vector<std::uint32_t>{0, 1, 3, 4, 5, 6, 8, 9, 10, 11});

	// Every pixel the held-out frames saw, and none of them in any problem: the adjustment's or a
	// resample's.
	std::set<std::pair<double, double>> heldPixels;
	for (const feature::Track& track : set.tracks)
	{
		for (const feature::SceneObservation& o : track.observations)
		{
			if (o.view == 2 || o.view == 7)
				heldPixels.insert({o.pixel.x, o.pixel.y});
		}
	}
	REQUIRE(heldPixels.size() > 100);
	const std::vector<standin::RefinerScript::Asked> calls = asked();
	REQUIRE(calls.size() == 1 + 4);
	for (const standin::RefinerScript::Asked& call : calls)
	{
		CHECK(call.poses <= 10);
		for (const math::Vec2d& p : call.pixels)
			CHECK(heldPixels.count({p.x, p.y}) == 0);
	}
	CHECK(calls[0].poses == 10);
}

TEST_CASE("a frame the placement cannot spare, or that seeds it, is never held out", "[camera][calibration][targetless]")
{
	SECTION("frame 2 seeds the reconstruction")
	{
		// Frames 0 and 1 miss ten landmarks every other frame sees, so frames 2 and 3 share the most
		// tracks of any pair: theirs is the anchor seed, and the stride's first frame is kept in.
		setUp();
		for (const std::size_t k : {0, 1})
		{
			for (std::size_t l = 0; l < 10; ++l)
				scene::scene().landmarks[l].hidden.insert({0, k});
		}
		const Report report = run(scene::sequenceTrackSetOf());
		INFO(report.toString());
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		CHECK(report.diagnostics.heldOut == std::vector<std::uint32_t>{7});
	}
	SECTION("frame 7 alone joins the frames after it")
	{
		// Frames 0 to 6 see landmarks 0 to 99; frame 7 sees twenty of them and 100 to 109; frames 8 to
		// 11 see 100 to 119; frame 6 sees 100 to 109 too. Without frame 7, landmarks 100 to 109 have
		// one placed frame, so frames 8 to 11 cannot be placed: the anchor seed's reach would be cut.
		setUp(12, 120);
		std::set<std::size_t> first, seventh, sixth, last;
		for (std::size_t l = 0; l < 100; ++l)
			first.insert(l);
		for (std::size_t l = 0; l < 20; ++l)
			seventh.insert(l * 5);
		for (std::size_t l = 100; l < 110; ++l)
		{
			seventh.insert(l);
			sixth.insert(l);
		}
		sixth.insert(first.begin(), first.end());
		for (std::size_t l = 100; l < 120; ++l)
			last.insert(l);
		for (std::size_t k = 0; k < 6; ++k)
			onlySees(k, first);
		onlySees(6, sixth);
		onlySees(7, seventh);
		for (std::size_t k = 8; k < 12; ++k)
			onlySees(k, last);
		const Report report = run(scene::sequenceTrackSetOf());
		INFO(report.toString());
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		CHECK(report.diagnostics.heldOut == std::vector<std::uint32_t>{2});
		CHECK(targetlessOf(report).unplaced.empty());
	}
}

TEST_CASE("a frame seeing too few landmarks is reported unplaced, and the calibration goes on without it",
		  "[camera][calibration][targetless]")
{
	setUp();
	onlySees(11, {0, 1, 2});
	const Report report = run(scene::sequenceTrackSetOf());
	INFO(report.toString());
	REQUIRE(report.status == CalibrationStatus::Succeeded);
	const TargetlessDiagnostics& t = targetlessOf(report);
	CHECK(t.unplaced == std::vector<std::uint32_t>{11});
	CHECK(std::count(t.placement.begin(), t.placement.end(), 11u) == 0);
	CHECK(report.diagnostics.selected.size() == 9);
	CHECK_THAT(report.toString(), ContainsSubstring("9 of 10 frames placed"));
	CHECK(asked().front().poses == 9);
}

TEST_CASE("footage that only turns is refused before any start, since a homography explains it",
		  "[camera][calibration][targetless]")
{
	// A homography explains every pair of a camera that only turns, whatever its model, so no pair may
	// seed and no start is tried. Without the test, with the truth between starts (500), the long start
	// posed five frames of this pan as an orbit, which only the verdict rejected.
	setUp();
	Request imported = pinholeRequest();
	imported.imported = pinhole(500);
	imported.importedPolicy = ImportedModelPolicy::Initial;
	for (const Request& request : {pinholeRequest(), imported})
	{
		INFO(name(request.importedPolicy));
		scene::resetPanning(12, 120, 1, pinhole(500));
		scene::scene().homographyThresholds.clear();
		const Report report = run(scene::sequenceTrackSetOf(), request);
		INFO(report.toString());
		REQUIRE(failedWith(report, Failure::InitialisationFailed));
		CHECK(report.failures.size() == 1);
		CHECK_THAT(report.failures.front().detail, ContainsSubstring("a homography explains 80%"));
		CHECK_THAT(report.failures.front().detail, ContainsSubstring("turned without moving"));
		const TargetlessDiagnostics& t = targetlessOf(report);
		// Every pair of the twelve frames shares every landmark.
		CHECK(t.seedPairs == 66);
		CHECK(t.seedPairsRefused == 66);
		CHECK(t.sweep.empty());
		CHECK(t.seed.empty());
		CHECK(report.diagnostics.heldOut.empty());
		CHECK(std::get<TargetlessRecord>(report.reproducibility.method).geometry.backend == "truth");
		// Tested once a pair, at the inlier distance in source pixels.
		CHECK(scene::scene().homographyThresholds == std::vector<double>(66, 4.0));
		CHECK(asked().empty());
	}
}

TEST_CASE("a seed that only turns still fails for its parallax when a homography explains too little of it",
		  "[camera][calibration][targetless]")
{
	// Moving content across a pan: a quarter of the landmarks move, so a homography explains 90 of each
	// pair's 120 tracks, under the 80% that refuses a seed, and the pan seeds. Under the camera's own
	// model (imported) most of the seed's matches are then points at infinity; the moving ones
	// triangulate, each at no less than the minimum parallax (a triangulation below it is a point at
	// infinity), so a median over the ones that triangulate would always clear the minimum, and the
	// refusal asks whether most matches are at infinity instead.
	setUp();
	scene::resetPanning(12, 120, 1, pinhole(500));
	for (std::size_t l = 0; l < 120; l += 4)
		scene::scene().landmarks[l].dynamic = true;
	Request imported = pinholeRequest();
	imported.imported = pinhole(500);
	imported.importedPolicy = ImportedModelPolicy::Initial;
	const Report report = run(scene::sequenceTrackSetOf(), imported);
	INFO(report.toString());
	REQUIRE(failedWith(report, Failure::InitialisationFailed));
	CHECK(report.failures.size() == 1);
	CHECK_THAT(report.failures.front().detail, ContainsSubstring("no parallax"));
	CHECK_THAT(report.failures.front().detail, ContainsSubstring("only turns"));
	const TargetlessDiagnostics& t = targetlessOf(report);
	CHECK(t.seedPairs == 66);
	CHECK(t.seedPairsRefused == 0);
	REQUIRE(t.seed.size() == 2);
	// Measured: 90 of 120 at infinity; the 30 moving landmarks triangulate at a median 38 mrad.
	CHECK(t.seedInliers > 0);
	CHECK(t.seedAtInfinity < t.seedInliers);
	CHECK(2 * t.seedAtInfinity >= t.seedInliers);
	CHECK(t.seedAngle >= feature::GeometryRequest{}.angle);
	CHECK(asked().empty());
}

TEST_CASE("a camera that moves is not refused, and a pair that only turns among its frames does not seed",
		  "[camera][calibration][targetless]")
{
	setUp();
	const Report moving = run(scene::sequenceTrackSetOf());
	INFO(moving.toString());
	REQUIRE(moving.status == CalibrationStatus::Succeeded);
	const TargetlessDiagnostics& t = targetlessOf(moving);
	CHECK(t.seedPairs == 66);
	CHECK(t.seedPairsRefused == 0);

	// The same wandering camera, its fourth frame turned in place where the third stood: those two
	// share their view of every landmark through a rotation alone, so the pair is refused, and the
	// calibration seeds elsewhere and places both.
	setUp();
	scene::scene().path[3] = math::RigidTransformd { scene::scene().path[3].rotation(),
													 scene::scene().path[2].translation() };
	const Report turned = run(scene::sequenceTrackSetOf());
	INFO(turned.toString());
	REQUIRE(turned.status == CalibrationStatus::Succeeded);
	const TargetlessDiagnostics& u = targetlessOf(turned);
	CHECK(u.seedPairs == 66);
	CHECK(u.seedPairsRefused == 1);
	REQUIRE(u.seed.size() == 2);
	CHECK(u.seed != std::vector<std::uint32_t>{2, 3});
	for (const std::uint32_t f : {2u, 3u})
	{
		CAPTURE(f);
		const std::vector<std::uint32_t>& heldOut = turned.diagnostics.heldOut;
		CHECK(std::count(u.placement.begin(), u.placement.end(), f) + std::count(heldOut.begin(), heldOut.end(), f) == 1);
	}
}

TEST_CASE("fewer than three frames placed is too few views", "[camera][calibration][targetless]")
{
	setUp(4, 120);
	onlySees(2, {0, 1, 2});
	onlySees(3, {0, 1, 2});
	const Report report = run(scene::sequenceTrackSetOf());
	INFO(report.toString());
	REQUIRE(failedWith(report, Failure::TooFewViews));
	CHECK_THAT(report.failures.front().detail, ContainsSubstring("2 of 4 frames"));
	CHECK(targetlessOf(report).unplaced == std::vector<std::uint32_t>{2, 3});
	CHECK(asked().empty());
}

TEST_CASE("the principal point is freed unless the settings hold it", "[camera][calibration][targetless]")
{
	setUp();
	const feature::TrackSet set = scene::sequenceTrackSetOf();
	const Report freed = run(set);
	REQUIRE(freed.status == CalibrationStatus::Succeeded);
	REQUIRE(std::holds_alternative<ResamplingEvidence>(freed.resampling));
	CHECK(std::holds_alternative<double>(std::get<ResamplingEvidence>(freed.resampling).principalPointVariation));
	CHECK(asked().front().modelFreedom.front().principalPoint);
	CHECK(std::get<TargetlessRecord>(freed.reproducibility.method).settings.freePrincipalPoint);

	targetless::Settings settings;
	settings.freePrincipalPoint = false;
	{
		std::lock_guard<std::mutex> lock(standin::refinerScript().mutex);
		standin::refinerScript().asked.clear();
	}
	const Report held = run(set, pinholeRequest(), settings);
	REQUIRE(held.status == CalibrationStatus::Succeeded);
	REQUIRE(std::holds_alternative<ResamplingEvidence>(held.resampling));
	CHECK(std::holds_alternative<Held>(std::get<ResamplingEvidence>(held.resampling).principalPointVariation));
	// Held is no shortfall: it adds no note, in either tier.
	for (const std::string& note : held.fitnessNotes)
		CHECK_THAT(note, !ContainsSubstring("principal"));
	CHECK_FALSE(asked().front().modelFreedom.front().principalPoint);
	CHECK_FALSE(std::get<TargetlessRecord>(held.reproducibility.method).settings.freePrincipalPoint);
}

TEST_CASE("an empty profile is the method's own, and either profile may judge it", "[camera][calibration][targetless]")
{
	CHECK(fitnessProfileNames() == std::vector<std::string>{"reconstruction/1", "reconstruction-targetless/1"});
	const std::optional<FitnessProfile> own = fitnessProfile("reconstruction-targetless/1");
	REQUIRE(own.has_value());
	CHECK(own->ready.minimumViews == 10);
	// The held-out residual is judged whitened by each feature's stated covariance, never as an angle.
	CHECK(std::isinf(own->ready.maximumHeldOutAngle));
	CHECK(std::isinf(own->exploratory.maximumHeldOutAngle));
	CHECK(own->ready.maximumHeldOutWhitened == 4.0);
	CHECK(own->exploratory.maximumHeldOutWhitened == 8.0);
	const std::optional<FitnessProfile> boards = fitnessProfile("reconstruction/1");
	REQUIRE(boards.has_value());
	CHECK(boards->ready.maximumHeldOutAngle == 0.0006);
	CHECK(std::isinf(boards->ready.maximumHeldOutWhitened));

	setUp();
	const feature::TrackSet set = scene::sequenceTrackSetOf();
	const Report defaulted = run(set);
	CHECK(defaulted.thresholds.name == "reconstruction-targetless/1");
	CHECK(defaulted.reproducibility.request.fitnessProfile.empty());
	Request request = pinholeRequest();
	request.fitnessProfile = "reconstruction/1";
	const Report board = run(set, request);
	REQUIRE(board.status == CalibrationStatus::Succeeded);
	CHECK(board.thresholds.name == "reconstruction/1");
}

TEST_CASE("targetless calibration is the same in parallel, serially, and given in another order",
		  "[camera][calibration][targetless]")
{
	setUp();
	scene::scene().landmarks[4].hidden.insert({0, 11}); // so not every frame sees every track
	const feature::TrackSet set = scene::sequenceTrackSetOf(0.3, 5);
	Request serial = pinholeRequest();
	serial.execution = ExecutionPolicy::DeterministicDebug;
	const Report a = run(set, serial);
	INFO(a.toString());
	REQUIRE(a.status == CalibrationStatus::Succeeded);
	REQUIRE(std::holds_alternative<HeldOutEvidence>(a.heldOut));
	CHECK(std::get<HeldOutEvidence>(a.heldOut).rmsAngle > 0); // noise, so held-out frames measure something

	const lain::testing::ThreadPool pool{4};
	requireSameReport(a, run(set));
	requireSameReport(a, run(reversed(set)));
}

TEST_CASE("calibrating footage is extracting its tracks, then calibrating them", "[camera][calibration][targetless]")
{
	setUp();
	for (std::size_t l = 0; l < 120; l += 12)
		scene::scene().landmarks[l].dynamic = true; // moving content: a stand-in's tracks keep it, as no
													// fundamental matrix sees along its epipolar lines
	const capture::CameraFootage footage = scene::sequenceFootage();
	const Request request = pinholeRequest();
	const Report report = targetless::calibrate(footage, targetless::Settings{}, request);
	INFO(report.toString());
	REQUIRE(report.status == CalibrationStatus::Succeeded);

	feature::SequenceExtractionRequest extraction;
	extraction.execution = request.execution;
	const feature::SequenceExtractionResult extracted = feature::extractSequenceTracks(footage, extraction);
	REQUIRE(extracted.ok());
	requireSameReport(report, run(extracted.trackSet, request));

	const TargetlessDiagnostics& t = targetlessOf(report);
	REQUIRE(t.extraction.has_value());
	CHECK(t.extraction->tracks == extracted.trackSet.tracks.size());
	CHECK(t.tracks == extracted.trackSet.tracks.size());
	CHECK(t.frames.size() == 12);
	const TargetlessRecord& record = std::get<TargetlessRecord>(report.reproducibility.method);
	CHECK(record.extractor.backend == "truth");
	CHECK(record.matcher.backend == "exact");
	CHECK(record.geometry.backend == "truth");
	CHECK(record.refiner.backend == "passthrough");
	CHECK(report.reproducibility.estimator.backend == "passthrough");
	CHECK(record.settings.extraction.execution == request.execution);
	CHECK(report.reproducibility.sources == std::vector<std::string>{scene::sourceOf(0)});
	CHECK(report.reproducibility.frames == 12);
}

TEST_CASE("a dataset or request that cannot calibrate is refused before any decode", "[camera][calibration][targetless]")
{
	setUp();
	const feature::TrackSet set = scene::sequenceTrackSetOf();
	const auto refusedBeforeDecoding = [](const Report& report, Failure failure)
	{
		INFO(report.toString());
		CHECK(failedWith(report, failure));
		CHECK(scene::scene().decodes.empty());
	};
	const auto footageRun = [](const Request& request, const targetless::Settings& settings = {})
	{ return targetless::calibrate(scene::sequenceFootage(), settings, request); };

	SECTION("empty footage")
	{
		scene::scene().frames = 0;
		refusedBeforeDecoding(footageRun(pinholeRequest()), Failure::NoFootage);
	}
	SECTION("an unknown profile")
	{
		Request request = pinholeRequest();
		request.fitnessProfile = "reconstruction-targetless/0";
		refusedBeforeDecoding(footageRun(request), Failure::UnknownFitnessProfile);
		CHECK(failedWith(run(set, request), Failure::UnknownFitnessProfile));
	}
	SECTION("HoldAndValidate with nothing to hold")
	{
		Request request = pinholeRequest();
		request.importedPolicy = ImportedModelPolicy::HoldAndValidate;
		refusedBeforeDecoding(footageRun(request), Failure::NoImportedModel);
	}
	SECTION("an imported model of another image")
	{
		CameraModelParameters p;
		p.image = {800, 600};
		p.intrinsics = {500.0, 500.0, 399.5, 299.5};
		Request request = pinholeRequest();
		request.imported = *CameraModel::create(p).model;
		request.importedPolicy = ImportedModelPolicy::Initial;
		refusedBeforeDecoding(footageRun(request), Failure::IncompatibleImportedModel);
		CHECK(failedWith(run(set, request), Failure::IncompatibleImportedModel));
	}
	SECTION("a model no refiner estimates")
	{
		Request request;
		request.model = DistortionModel::RationalBrownConrady8;
		const Report report = footageRun(request);
		refusedBeforeDecoding(report, Failure::NoEstimator);
		CHECK_THAT(report.failures.front().detail, ContainsSubstring("Rational"));
	}
	SECTION("an extraction the calibration's own policy refuses")
	{
		targetless::Settings settings;
		settings.extraction.matching.search = feature::MatchSearch::Approximate;
		Request request = pinholeRequest();
		request.execution = ExecutionPolicy::DeterministicDebug;
		const Report report = footageRun(request, settings);
		REQUIRE(failedWith(report, Failure::ExtractionFailed));
		CHECK_THAT(report.failures.front().detail, ContainsSubstring("pproximate"));
		// The record says what it would have extracted with, under the calibration's policy.
		CHECK(std::get<TargetlessRecord>(report.reproducibility.method).settings.extraction.execution ==
			  ExecutionPolicy::DeterministicDebug);
	}
	SECTION("footage of a camera with no identity")
	{
		capture::CameraFootage footage = scene::sequenceFootage();
		footage.camera = capture::CameraIdentity{};
		CHECK(failedWith(targetless::calibrate(footage, {}, pinholeRequest()), Failure::InvalidDataset));
	}
	SECTION("a track set of no frame")
	{
		CHECK(failedWith(run(feature::TrackSet{}), Failure::NoFootage));
		feature::TrackSet bad = set;
		for (feature::View& v : bad.views)
			v.frames.clear();
		bad.tracks.clear();
		CHECK(failedWith(run(bad), Failure::NoFootage));
	}
	SECTION("views of two cameras")
	{
		feature::TrackSet bad = set;
		bad.views[3].camera = capture::CameraIdentity{"cam01"};
		CHECK(failedWith(run(bad), Failure::InvalidDataset));
	}
	SECTION("views of two image sizes")
	{
		feature::TrackSet bad = set;
		bad.views[3].image = ImageGeometry{800, 600};
		CHECK(failedWith(run(bad), Failure::InvalidDataset));
	}
	SECTION("a view of two frames")
	{
		feature::TrackSet bad = set;
		bad.views[3].frames.push_back(bad.views[4].frames.front());
		CHECK(failedWith(run(bad), Failure::InvalidDataset));
	}
	SECTION("a frame given twice")
	{
		feature::TrackSet bad = set;
		bad.views[3].frames = bad.views[4].frames;
		const Report report = run(bad);
		REQUIRE(failedWith(report, Failure::InvalidDataset));
		CHECK_THAT(report.failures.front().detail, ContainsSubstring("twice"));
	}
	SECTION("an observation of a view the set does not have")
	{
		feature::TrackSet bad = set;
		bad.tracks[3].observations.back().view = 99;
		CHECK(failedWith(run(bad), Failure::InvalidDataset));
	}
	SECTION("an observation of a view holding no frame")
	{
		feature::TrackSet bad = set;
		bad.views[5].frames.clear();
		CHECK(failedWith(run(bad), Failure::InvalidDataset));
	}
	SECTION("two observations of one view in a track")
	{
		feature::TrackSet bad = set;
		bad.tracks[3].observations[1].view = bad.tracks[3].observations[0].view;
		CHECK(failedWith(run(bad), Failure::InvalidDataset));
	}
	SECTION("a covariance that is not positive definite")
	{
		feature::TrackSet bad = set;
		bad.tracks[3].observations[0].covariance = std::array<double, 3>{1.0, 2.0, 1.0};
		CHECK(failedWith(run(bad), Failure::InvalidDataset));
	}
	SECTION("a track given twice")
	{
		feature::TrackSet bad = set;
		bad.tracks[4].identity = bad.tracks[3].identity;
		CHECK(failedWith(run(bad), Failure::InvalidDataset));
	}
	// Every refusal is an ordinary report: it holds what it was asked.
	CHECK(asked().empty());
}
