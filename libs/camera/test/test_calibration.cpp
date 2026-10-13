// The board calibration method module through its production entry point, calibrate(), over
// stand-in backends answering from a known truth (syntheticboard.h). Its own executable: the
// registries only grow, and these stand-ins must be the only backends registered.

#include "syntheticboard.h"

#include <lain/camera/calibration/board.h>
#include <lain/camera/calibration/fitness.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::calibration;
using namespace lain::camera::testing;
namespace method = lain::camera::calibration::board;

namespace
{
	Report run(const Request& request = {})
	{
		return method::calibrate(footage(scene().frames), specification(), camera::board::DetectionRequest{}, request);
	}

	// A search unlike the default in every field.
	camera::board::DetectionRequest unusualSearch()
	{
		camera::board::DetectionRequest search;
		search.scale = camera::board::LongestSide{800};
		search.refineAtNativeResolution = false;
		search.detail = camera::board::DetailLevel::Detailed;
		search.minimumCorners = 6;
		return search;
	}

	bool isUnusualSearch(const camera::board::DetectionRequest& search)
	{
		const auto* longest = std::get_if<camera::board::LongestSide>(&search.scale);
		return longest != nullptr && longest->pixels == 800 && !search.refineAtNativeResolution &&
			   search.detail == camera::board::DetailLevel::Detailed && search.minimumCorners == 6;
	}

	const BoardRecord& boardRecord(const Report& report)
	{
		return std::get<BoardRecord>(report.reproducibility.method);
	}

	const BoardDiagnostics& boardDiagnostics(const Report& report)
	{
		return std::get<BoardDiagnostics>(report.diagnostics.method);
	}

	bool failedWith(const Report& report, Failure failure)
	{
		if (report.status != CalibrationStatus::Failed || report.model.has_value())
			return false;
		for (const FailureReason& f : report.failures)
		{
			if (f.failure == failure)
				return true;
		}
		return false;
	}
} // namespace

TEST_CASE("the built-in fitness profile resolves, with overrides on its Ready tier only", "[camera][calibration]")
{
	const std::optional<FitnessProfile> profile = fitnessProfile("reconstruction/1");
	REQUIRE(profile.has_value());
	CHECK_FALSE(fitnessProfile("reconstruction/0").has_value());
	CHECK(fitnessProfileNames() == std::vector<std::string>{"reconstruction/1"});

	FitnessOverrides overrides;
	overrides.minimumViews = 25;
	const FitnessProfile resolved = resolve(*profile, overrides);
	CHECK(resolved.ready.minimumViews == 25);
	CHECK(resolved.ready.minimumCoverage == profile->ready.minimumCoverage);
	CHECK(resolved.exploratory.minimumViews == profile->exploratory.minimumViews);
}

TEST_CASE("a clean scene calibrates Ready, with every section present", "[camera][calibration]")
{
	resetScene();
	const Report report = run();
	REQUIRE(report.status == CalibrationStatus::Succeeded);
	REQUIRE(report.model.has_value());
	CHECK(report.verdict == Verdict::Ready);
	INFO("notes: " << (report.fitnessNotes.empty() ? std::string("none") : report.fitnessNotes.front()));
	CHECK(report.fitnessNotes.empty());

	const Diagnostics& d = report.diagnostics;
	CHECK(boardDiagnostics(report).framesExamined == 40);
	CHECK(boardDiagnostics(report).framesUsable == 40);
	CHECK(boardDiagnostics(report).detections.size() == 40);
	CHECK(d.viewsHeldOut == 8); // every fifth of 40
	CHECK(d.viewsSelected == 30);
	CHECK(d.coverage > 0.6);

	// Held-out views take no part in estimation.
	for (const std::uint32_t h : d.heldOut)
		CHECK(std::find(d.selected.begin(), d.selected.end(), h) == d.selected.end());

	// Validation measured the true camera against exact projections: nothing to see.
	const auto* heldOut = std::get_if<HeldOutEvidence>(&report.heldOut);
	REQUIRE(heldOut != nullptr);
	CHECK(heldOut->views == 8);
	CHECK(heldOut->viewsWithoutPose == 0);
	CHECK(heldOut->rmsPixels < 1e-6);
	CHECK(heldOut->rmsAngle < 1e-9);

	const auto* resampling = std::get_if<ResamplingEvidence>(&report.resampling);
	REQUIRE(resampling != nullptr);
	CHECK(resampling->resamples == 20);
	CHECK(resampling->focalVariation == 0.0);
	// Board calibration estimates the principal point, so it always measures how it varies.
	const double* principalPoint = std::get_if<double>(&resampling->principalPointVariation);
	REQUIRE(principalPoint != nullptr);
	CHECK(*principalPoint < 1e-12);

	CHECK(report.thresholds.name == "reconstruction/1");
	CHECK(report.reproducibility.sources == std::vector<std::string>{"/synthetic/calibration"});
	CHECK(boardRecord(report).detector.backend == "truth");
	CHECK(report.reproducibility.estimator.backend == "scripted");
	CHECK(report.reproducibility.request.seed == 1);
}

TEST_CASE("footage is searched as asked, and the record says how", "[camera][calibration]")
{
	resetScene();
	const Report report = method::calibrate(footage(scene().frames), specification(), unusualSearch(), Request{});
	REQUIRE(report.status == CalibrationStatus::Succeeded);

	// The search reaches every frame's detection, and the record says what it was.
	REQUIRE(boardDiagnostics(report).detections.size() == 40);
	for (const camera::board::DetectionReport& d : boardDiagnostics(report).detections)
		CHECK(isUnusualSearch(d.request));
	REQUIRE(boardRecord(report).detection.has_value());
	CHECK(isUnusualSearch(*boardRecord(report).detection));

	SECTION("a request refused before anything is detected still says what would have been searched with")
	{
		Request request;
		request.fitnessProfile = "reconstruction/99";
		const Report refused = method::calibrate(footage(scene().frames), specification(), unusualSearch(), request);
		REQUIRE(failedWith(refused, Failure::UnknownFitnessProfile));
		CHECK(boardDiagnostics(refused).detections.empty());
		REQUIRE(boardRecord(refused).detection.has_value());
		CHECK(isUnusualSearch(*boardRecord(refused).detection));
	}
}

TEST_CASE("a calibration over given detections records the search they report", "[camera][calibration]")
{
	resetScene();
	const media::FrameSequence frames = footage(scene().frames);
	std::vector<camera::board::DetectionReport> detections;
	for (std::size_t i = 0; i < frames.size(); ++i)
		detections.push_back(camera::board::detect(frames.image(i), frames.frame(i), specification(), unusualSearch()));

	// The calibration's own request holds no search at all: what the detections were made with is
	// theirs to say.
	const Report report = method::calibrate(detections, trueCamera().image, specification(), Request{});
	REQUIRE(report.status == CalibrationStatus::Succeeded);
	REQUIRE(boardRecord(report).detection.has_value());
	CHECK(isUnusualSearch(*boardRecord(report).detection));
	CHECK(boardRecord(report).detector.backend == "truth");
}

TEST_CASE("a model whose held-out rays miss is not Ready, and the notes say why", "[camera][calibration]")
{
	// The estimator reports a camera 2% long in focal length: consistent with itself, and wrong.
	resetScene();
	script().answer = [](const std::vector<camera::board::Observation>&)
	{
		CameraModelParameters p = trueCamera();
		p.intrinsics.fx *= 1.02;
		p.intrinsics.fy *= 1.02;
		return std::optional<CameraModelParameters>{p};
	};
	const Report report = run();
	REQUIRE(report.status == CalibrationStatus::Succeeded);
	const auto* heldOut = std::get_if<HeldOutEvidence>(&report.heldOut);
	REQUIRE(heldOut != nullptr);
	CHECK(heldOut->rmsAngle > report.thresholds.ready.maximumHeldOutAngle);
	CHECK(report.verdict != Verdict::Ready);
	CHECK(std::any_of(report.fitnessNotes.begin(), report.fitnessNotes.end(),
					  [](const std::string& note)
					  { return note.find("held-out RMS angle") != std::string::npos; }));
}

TEST_CASE("the resampling draws are seeded, and the same under either execution policy", "[camera][calibration]")
{
	// The stand-in's focal length depends on WHICH views it was handed, so the stability evidence is
	// a fingerprint of the draws.
	resetScene();
	script().answer = [](const std::vector<camera::board::Observation>& views)
	{
		std::set<std::size_t> distinct;
		for (const camera::board::Observation& v : views)
			distinct.insert(v.frame.ordinal);
		CameraModelParameters p = trueCamera();
		p.intrinsics.fx += double(distinct.size());
		return std::optional<CameraModelParameters>{p};
	};
	Request request;
	std::size_t normalThreads = 0;
	Report normal;
	{
		// A started pool, or "Normal" would run inline and prove nothing about parallel execution.
		const lain::testing::ThreadPool pool{3};
		normal = run(request);
		normalThreads = scene().threads.size();
	}
	scene().threads.clear();
	request.execution = ExecutionPolicy::DeterministicDebug;
	const Report serial = run(request);
	CHECK(normalThreads > 1);			// frames really were detected on several threads...
	CHECK(scene().threads.size() == 1); // ...and deterministic-debug really is serial
	request.seed = 2;
	const Report reseeded = run(request);

	const double a = std::get<ResamplingEvidence>(normal.resampling).focalVariation;
	const double b = std::get<ResamplingEvidence>(serial.resampling).focalVariation;
	const double c = std::get<ResamplingEvidence>(reseeded.resampling).focalVariation;
	CHECK(a > 0);
	CHECK(a == b);
	CHECK(a != c);
	CHECK(normal.diagnostics.selected == serial.diagnostics.selected);
	CHECK(normal.diagnostics.heldOut == serial.diagnostics.heldOut);
}

TEST_CASE("frames without a board are examined and never chosen", "[camera][calibration]")
{
	resetScene();
	scene().boardless = {0, 1, 2, 17, 33};
	const Report report = run();
	REQUIRE(report.status == CalibrationStatus::Succeeded);
	CHECK(boardDiagnostics(report).framesExamined == 40);
	CHECK(boardDiagnostics(report).framesUsable == 35);
	for (const std::size_t skip : scene().boardless)
	{
		const std::uint32_t position = std::uint32_t(skip);
		CHECK(std::find(report.diagnostics.selected.begin(), report.diagnostics.selected.end(), position) ==
			  report.diagnostics.selected.end());
		CHECK(std::find(report.diagnostics.heldOut.begin(), report.diagnostics.heldOut.end(), position) ==
			  report.diagnostics.heldOut.end());
		CHECK(boardDiagnostics(report).detections[skip].status == camera::board::DetectionStatus::Failed);
	}
}

TEST_CASE("an imported model seeds only what is the same model", "[camera][calibration]")
{
	resetScene();
	Request request;
	request.importedPolicy = ImportedModelPolicy::Initial;

	SECTION("the same distortion model seeds everything")
	{
		request.imported = *CameraModel::create(trueCamera()).model;
		const Report report = run(request);
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		CHECK(report.seededFields == std::vector<std::string>{"fx", "fy", "cx", "cy", "k1", "k2", "p1", "p2", "k3"});
		REQUIRE(script().lastInitial.has_value());
		CHECK(std::get<BrownConrady5>(script().lastInitial->distortion).k1 == -0.1);
		// Every resample started from the same seed as the estimate: stability is measured for the
		// procedure that produced the model. (The stand-in records the LAST call, which is a resample.)
		CHECK(script().estimates == 21);
	}
	SECTION("a different model seeds the pinhole part, and its coefficients start neutral")
	{
		CameraModelParameters inverse = trueCamera();
		inverse.distortion = InverseBrownConrady5{0.1, -0.05, 0.001, -0.0005, 0.0};
		request.imported = *CameraModel::create(inverse).model;
		const Report report = run(request);
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		CHECK(report.seededFields == std::vector<std::string>{"fx", "fy", "cx", "cy"});
		REQUIRE(script().lastInitial.has_value());
		const BrownConrady5& seeded = std::get<BrownConrady5>(script().lastInitial->distortion);
		CHECK(seeded.k1 == 0.0); // never copied across on the shape of five numbers
		CHECK(script().lastInitial->intrinsics.fx == 900.0);
	}
}

TEST_CASE("a held model is validated, never estimated, and cannot be Ready", "[camera][calibration]")
{
	resetScene();
	Request request;
	request.importedPolicy = ImportedModelPolicy::HoldAndValidate;
	request.imported = *CameraModel::create(trueCamera()).model;
	const Report report = run(request);
	REQUIRE(report.status == CalibrationStatus::Succeeded);
	CHECK(script().estimates == 0);
	CHECK(report.diagnostics.viewsSelected == 0);
	CHECK(report.diagnostics.viewsHeldOut == 40); // every usable view validates it
	CHECK(std::holds_alternative<HeldOutEvidence>(report.heldOut));
	CHECK(std::holds_alternative<Unavailable>(report.resampling));
	// Ready needs demonstrated stability, and a held model has none to demonstrate.
	CHECK(report.verdict == Verdict::Exploratory);
}

TEST_CASE("every failure is a report with a reason, never a throw", "[camera][calibration]")
{
	SECTION("an unknown fitness profile")
	{
		resetScene();
		Request request;
		request.fitnessProfile = "reconstruction/99";
		CHECK(failedWith(run(request), Failure::UnknownFitnessProfile));
	}
	SECTION("a model no backend estimates, never a similar one instead")
	{
		resetScene();
		Request request;
		request.model = DistortionModel::RationalBrownConrady8;
		const Report report = run(request);
		CHECK(failedWith(report, Failure::NoEstimator));
		CHECK(script().estimates == 0);
	}
	SECTION("no footage")
	{
		resetScene();
		scene().frames = 0;
		CHECK(failedWith(run(), Failure::NoFootage));
	}
	SECTION("holding a model that was not given")
	{
		resetScene();
		Request request;
		request.importedPolicy = ImportedModelPolicy::HoldAndValidate;
		CHECK(failedWith(run(request), Failure::NoImportedModel));
	}
	SECTION("an imported model of another geometry")
	{
		resetScene();
		CameraModelParameters other = trueCamera();
		other.image = {1920, 1080};
		Request request;
		request.imported = *CameraModel::create(other).model;
		request.importedPolicy = ImportedModelPolicy::Initial;
		CHECK(failedWith(run(request), Failure::IncompatibleImportedModel));
	}
	SECTION("too few usable views")
	{
		resetScene();
		scene().frames = 2;
		CHECK(failedWith(run(), Failure::TooFewViews));
	}
	SECTION("an estimator that fails")
	{
		resetScene();
		script().answer = [](const std::vector<camera::board::Observation>&)
		{ return std::optional<CameraModelParameters>{}; };
		CHECK(failedWith(run(), Failure::EstimationFailed));
	}
	SECTION("an estimate that is not a camera")
	{
		resetScene();
		script().answer = [](const std::vector<camera::board::Observation>&)
		{
			CameraModelParameters p = trueCamera();
			p.intrinsics.fx = -900.0;
			return std::optional<CameraModelParameters>{p};
		};
		CHECK(failedWith(run(), Failure::InvalidModel));
	}
}

TEST_CASE("view selection is canonical, capped, and leaves held-out views alone", "[camera][calibration]")
{
	resetScene();
	const camera::board::Specification spec = specification();
	std::vector<camera::board::DetectionReport> detections;
	TruthDetector detector;
	for (std::size_t i = 0; i < 40; ++i)
	{
		media::FrameRef frame;
		frame.source = core::Uri{"/synthetic/calibration"};
		frame.ordinal = i;
		detections.push_back(detector.detect({}, frame, spec, {}, 1.0));
	}

	Request request;
	const method::ViewSplit split = method::splitViews(detections, spec, request);
	CHECK(split.heldOut == std::vector<std::uint32_t>{2, 7, 12, 17, 22, 27, 32, 37});
	CHECK(split.calibration.size() == 30);

	// The same frames in another order choose the same frames.
	std::vector<camera::board::DetectionReport> shuffled(detections.rbegin(), detections.rend());
	const method::ViewSplit again = method::splitViews(shuffled, spec, request);
	std::vector<std::size_t> first, second;
	for (const std::uint32_t p : split.calibration)
		first.push_back(detections[p].observation->frame.ordinal);
	for (const std::uint32_t p : again.calibration)
		second.push_back(shuffled[p].observation->frame.ordinal);
	CHECK(first == second);

	request.maximumViews = 5;
	CHECK(method::splitViews(detections, spec, request).calibration.size() == 5);

	// Fewer than five usable views: none are held out.
	detections.resize(4);
	CHECK(method::splitViews(detections, spec, Request{}).heldOut.empty());
}

TEST_CASE("coverage counts the image-grid cells the views reach", "[camera][calibration]")
{
	camera::board::Observation corner;
	corner.image = {800, 600};
	corner.features = {{0, {10.0, 10.0}, std::nullopt}}; // the top-left cell
	camera::board::Observation opposite = corner;
	opposite.features = {{0, {790.0, 590.0}, std::nullopt}, {1, {795.0, 595.0}, std::nullopt}}; // one cell, twice
	camera::board::Observation outside = corner;
	outside.features = {{0, {-5.0, 300.0}, std::nullopt}}; // off the image: no cell
	CHECK(method::coverage({&corner}, corner.image) == Catch::Approx(1.0 / 48));
	CHECK(method::coverage({&corner, &opposite, &outside}, corner.image) == Catch::Approx(2.0 / 48));
}

// --- judge: the verdict over hand-built evidence -------------------------------------
//
// Nothing produces a held principal point until targetless calibration (M9 slice 4, sub-slice 9),
// so the rule is tested here, on the function both methods call. Every variant is constructed, never
// reassigned: GCC 13 at -O3 raised a false -Wmaybe-uninitialized on a reassigned report variant in
// registration's slice 3.

namespace
{
	FitnessProfile reconstruction1()
	{
		const std::optional<FitnessProfile> profile = fitnessProfile("reconstruction/1");
		REQUIRE(profile.has_value());
		return *profile;
	}

	// Well inside reconstruction/1's Ready tier (0.6 mrad).
	std::variant<HeldOutEvidence, Unavailable> goodHeldOut()
	{
		return HeldOutEvidence{8, 192, 0, 0.0003, 0.3, 0.6};
	}

	bool startsWith(const std::string& text, const std::string& prefix)
	{
		return text.compare(0, prefix.size(), prefix) == 0;
	}
} // namespace

TEST_CASE("a held principal point does not keep an estimate from Ready", "[camera][calibration][judge]")
{
	const std::variant<ResamplingEvidence, Unavailable> resampling{ResamplingEvidence{10, 0.001, Held{}}};
	const Judgement judgement = judge(reconstruction1(), 20, 0.9, goodHeldOut(), resampling);
	INFO("notes: " << (judgement.notes.empty() ? std::string("none") : judgement.notes.front()));
	CHECK(judgement.verdict == Verdict::Ready);
	CHECK(judgement.notes.empty());
}

TEST_CASE("a held principal point beside an unstable focal length is noted for the focal length alone",
		  "[camera][calibration][judge]")
{
	// 1% is over Ready's 0.5% and inside Exploratory's 2%.
	const std::variant<ResamplingEvidence, Unavailable> resampling{ResamplingEvidence{10, 0.01, Held{}}};
	const Judgement judgement = judge(reconstruction1(), 20, 0.9, goodHeldOut(), resampling);
	CHECK(judgement.verdict == Verdict::Exploratory);
	REQUIRE(judgement.notes.size() == 1);
	CHECK(startsWith(judgement.notes[0], "Ready: focal variation "));
}

TEST_CASE("missing resampling evidence refuses Ready with its reason and leaves Exploratory open",
		  "[camera][calibration][judge]")
{
	const std::variant<ResamplingEvidence, Unavailable> resampling{Unavailable{"resampling was not requested"}};
	const Judgement judgement = judge(reconstruction1(), 20, 0.9, goodHeldOut(), resampling);
	CHECK(judgement.verdict == Verdict::Exploratory);
	REQUIRE(judgement.notes.size() == 1);
	CHECK(judgement.notes[0] == "Ready: no resampling evidence: resampling was not requested");
}

TEST_CASE("a rejected estimate's notes give Ready's shortfalls, then Exploratory's", "[camera][calibration][judge]")
{
	// Every criterion fails both tiers: 4 views, 20% coverage, 3 mrad held out, 3% focal and 20 mrad of
	// principal point.
	const std::variant<HeldOutEvidence, Unavailable> heldOut{HeldOutEvidence{8, 192, 0, 0.003, 3.0, 6.0}};
	const std::variant<ResamplingEvidence, Unavailable> resampling{ResamplingEvidence{10, 0.03, 0.02}};
	const Judgement judgement = judge(reconstruction1(), 4, 0.2, heldOut, resampling);
	CHECK(judgement.verdict == Verdict::Rejected);
	const std::vector<std::string> expected = {"views, fewer than", "coverage ", "held-out RMS angle ",
											   "focal variation ", "principal point variation "};
	REQUIRE(judgement.notes.size() == 2 * expected.size());
	for (std::size_t i = 0; i < expected.size(); ++i)
	{
		INFO("note " << i << ": " << judgement.notes[i] << " / " << judgement.notes[expected.size() + i]);
		CHECK(startsWith(judgement.notes[i], "Ready: "));
		CHECK(judgement.notes[i].find(expected[i]) != std::string::npos);
		CHECK(startsWith(judgement.notes[expected.size() + i], "Exploratory: "));
		CHECK(judgement.notes[expected.size() + i].find(expected[i]) != std::string::npos);
	}
}
