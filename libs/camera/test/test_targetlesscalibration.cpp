// Targetless calibration (calibration::targetless) over stand-ins answering from a known moving camera
// (syntheticscene.h) and a refiner that hands back its start (passthroughrefiner.h): the focal sweep,
// the starting model under each imported-model policy, the held-out frames, which frames are placed,
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
	// every candidate of the focal sweep has, so one at a candidate's focal length is that candidate.
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

	// The sweep's step: candidates are log-spaced, so each is this factor longer than the one before.
	double sweepStep(const TargetlessDiagnostics& t)
	{
		return t.sweep[1].focalLength / t.sweep[0].focalLength;
	}
} // namespace

TEST_CASE("the focal sweep starts within one candidate of the truth, and records every score",
		  "[camera][calibration][targetless]")
{
	// The pinhole candidates at 640x480 run from 184.75 (120 degrees across) to 1443.4 (25 degrees),
	// 10.83% apart; the fisheye's from 305.6 to 1466.7, 8.16% apart, through its own neutral projection.
	struct Case
	{
		CameraModel truth;
		DistortionModel model;
		double step;
	};
	for (const Case& c : {Case{pinhole(500), DistortionModel::None, 1.10826}, Case{pinhole(400), DistortionModel::None, 1.10826},
						  Case{pinhole(625), DistortionModel::None, 1.10826},
						  Case{equidistant(500), DistortionModel::KannalaBrandt4, 1.08159}})
	{
		const double f = c.truth.intrinsics().fx;
		DYNAMIC_SECTION(std::string(displayName(c.truth.distortion())) << " at " << f)
		{
			setUp(12, 120, c.truth);
			Request request;
			request.model = c.model;
			const Report report = run(scene::sequenceTrackSetOf(), request);
			INFO(report.toString());
			const TargetlessDiagnostics& t = targetlessOf(report);
			REQUIRE(t.sweep.size() == 21);
			CHECK(std::abs(sweepStep(t) - c.step) < 1e-4);
			for (std::size_t i = 1; i < t.sweep.size(); ++i)
				CHECK(t.sweep[i].focalLength > t.sweep[i - 1].focalLength);
			REQUIRE(t.swept.has_value());
			const double chosen = t.sweep[*t.swept].focalLength;
			CHECK(std::abs(std::log(chosen / f)) < std::log(c.step));
			// The most inliers wins, and a tie goes to the shorter focal length.
			for (std::size_t i = 0; i < t.sweep.size(); ++i)
			{
				if (i < *t.swept)
					CHECK(t.sweep[i].inliers < t.sweep[*t.swept].inliers);
				else
					CHECK(t.sweep[i].inliers <= t.sweep[*t.swept].inliers);
			}
			// Each candidate scored at its own angle: 4 px at its focal length.
			CHECK(report.seededFields.empty());
			REQUIRE(report.model.has_value());
			CHECK(report.model->intrinsics().fx == chosen);
			CHECK(report.model->intrinsics().cx == 319.5);
			CHECK(modelOf(report.model->distortion()) == c.model);
		}
	}
}

TEST_CASE("each sweep candidate is scored at 4 pixels of its own focal length", "[camera][calibration][targetless]")
{
	setUp();
	scene::scene().relativeAngles.clear();
	const Report report = run(scene::sequenceTrackSetOf());
	const TargetlessDiagnostics& t = targetlessOf(report);
	REQUIRE(t.sweep.size() == 21);
	// Every angle the stand-in was handed is 4 px at one candidate's focal length, or at the starting
	// model's for the seed; and every candidate's appears.
	std::set<double> handed(scene::scene().relativeAngles.begin(), scene::scene().relativeAngles.end());
	for (const SweepCandidate& c : t.sweep)
		CHECK(handed.count(4.0 / c.focalLength) == 1);
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

TEST_CASE("footage that only turns fails to start, naming its parallax", "[camera][calibration][targetless]")
{
	// Under a model whose rays are right, a camera that only turns sees every match as a point at
	// infinity, whatever baseline its relative pose is given. Two such models: an imported one, and the
	// sweep's own candidate when the camera's focal length is one (here the eleventh, 516.4).
	//
	// Recorded, not pinned: with the truth BETWEEN candidates (500), the sweep starts at 516.4, and its
	// rays, 3% off, are explained better by a pan of another angle plus the stand-in's arbitrary
	// sideways baseline than by the true pan: all 120 seed matches triangulate, at a median 52 mrad,
	// and the calibration goes on to a spurious orbit that only its verdict rejects (5.3 mrad held out).
	// A pure rotation is told apart for any model by whether a rotation alone explains a pair, which
	// this method does not ask yet.
	//
	// Moving content is the second half: a landmark moving across a pan has real parallax, so a few
	// matches triangulate, each at no less than the minimum parallax (a triangulation below it is a
	// point at infinity). A median over the ones that triangulate would then always clear the minimum,
	// so the refusal asks whether most matches are at infinity instead.
	setUp();
	scene::resetPanning(12, 120, 1, pinhole(500));
	const double candidate = targetlessOf(run(scene::sequenceTrackSetOf())).sweep.at(10).focalLength;
	Request imported = pinholeRequest();
	imported.imported = pinhole(candidate);
	imported.importedPolicy = ImportedModelPolicy::Initial;
	for (const bool moving : {false, true})
	{
		for (const Request& request : {pinholeRequest(), imported})
		{
			scene::resetPanning(12, 120, 1, pinhole(candidate));
			if (moving)
			{
				for (std::size_t l = 0; l < 120; l += 10)
					scene::scene().landmarks[l].dynamic = true;
			}
			{
				std::lock_guard<std::mutex> lock(standin::refinerScript().mutex);
				standin::refinerScript().asked.clear();
			}
			const Report report = run(scene::sequenceTrackSetOf(), request);
			INFO(report.toString());
			INFO("moving content " << moving << ", " << name(request.importedPolicy));
			REQUIRE(failedWith(report, Failure::InitialisationFailed));
			CHECK_THAT(report.failures.front().detail, ContainsSubstring("no parallax"));
			CHECK_THAT(report.failures.front().detail, ContainsSubstring("only turns"));
			const TargetlessDiagnostics& t = targetlessOf(report);
			CHECK(t.seed.size() == 2);
			CHECK(t.seedInliers > 0);
			if (!moving)
				CHECK(t.seedAtInfinity == t.seedInliers);
			else
			{
				// Measured: 108 of 120 at infinity; the 12 moving landmarks triangulate at a median 64 mrad.
				CHECK(t.seedAtInfinity < t.seedInliers);
				CHECK(t.seedAngle >= feature::GeometryRequest{}.angle);
			}
			if (request.importedPolicy == ImportedModelPolicy::Ignore)
				CHECK(t.swept == std::optional<std::uint32_t>{10});
			CHECK(asked().empty());
		}
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
	CHECK(own->ready.maximumHeldOutAngle == 0.0015);
	CHECK(own->exploratory.maximumHeldOutAngle == 0.005);

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
