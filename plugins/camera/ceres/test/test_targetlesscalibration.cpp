// Targetless calibration (calibration::targetless) through the Ceres refiner, over the stand-in scene
// and geometry (syntheticscene.h): one moving camera's model recovered from its frames' tracks, exact
// and noisy; a wrong match and moving content named and kept from moving the model or the fit RMS; a
// resample leaving out what the refined model cannot project; a principal point recovered as it is by
// default, and missed when held; and one report however the work ran or the tracks are named. The
// stand-in geometry proposes only true poses, so it cannot place a frame a starting model misses by
// much; a principal point (8, -6) px off is placed, and recovered exactly (sub-slice 10b). A model is compared
// with the truth by its ray mapping, never by its coefficients. Every tolerance is a measurement,
// written beside its check; Debug and Release measure the same. Every case but the one comparing the
// two starts a pool of four, since without one the resamples run inline on the case's thread.

#include "syntheticscene.h"
#include "targetlesscalibrationtext.h"

#include <lain/camera/calibration/targetless.h>
#include <lain/camera/ceres/register.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <variant>
#include <vector>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::calibration;
namespace scene = lain::camera::testing::scene;
using lain::camera::testing::targetlesstext::requireSameReport;

namespace
{
	void setUp(const CameraModel& truth, std::size_t frames = 12, std::size_t landmarks = 120)
	{
		static const bool once = []
		{
			lain::camera::ceres::registerBackend();
			return true;
		}();
		(void)once;
		scene::registerStandIns();
		scene::resetWandering(frames, landmarks, 1, truth);
	}

	// A camera at 640x480, as the stand-in scene's.
	CameraModel cameraOf(double fx, double fy, double cx, double cy, const Distortion& distortion = NoDistortion{})
	{
		CameraModelParameters p;
		p.image = {640, 480};
		p.intrinsics = {fx, fy, cx, cy};
		p.distortion = distortion;
		const ModelResult result = CameraModel::create(p);
		REQUIRE(result.model.has_value());
		return *result.model;
	}

	// A pinhole with f = 500 and its principal point at the image's centre.
	CameraModel centred()
	{
		return cameraOf(500, 500, 319.5, 239.5);
	}

	Request requestFor(DistortionModel model)
	{
		Request request;
		request.model = model;
		return request;
	}

	Report run(const feature::TrackSet& tracks, const Request& request = requestFor(DistortionModel::None),
			   const targetless::Settings& settings = {})
	{
		return targetless::calibrate(tracks, settings, request);
	}

	const TargetlessDiagnostics& targetlessOf(const Report& report)
	{
		return std::get<TargetlessDiagnostics>(report.diagnostics.method);
	}

	// How far an estimate is from the truth: the focal lengths' relative error, and the ray mapping's,
	// every 16th pixel's true ray projected through the estimate, in pixels (test_freemodel.cpp's).
	struct ModelError
	{
		double focal = 0;
		double ray = 0;
	};
	ModelError errorOf(const CameraModel& estimate, const CameraModel& truth)
	{
		ModelError out;
		out.focal = std::max(std::abs(estimate.intrinsics().fx / truth.intrinsics().fx - 1),
							 std::abs(estimate.intrinsics().fy / truth.intrinsics().fy - 1));
		for (double v = 0; v < double(truth.image().height); v += 16)
		{
			for (double u = 0; u < double(truth.image().width); u += 16)
			{
				const Unprojection<double> ray = unproject(truth, u, v);
				REQUIRE(ray.ok());
				const Projection<double> p = project(estimate, ray.x, ray.y, ray.z);
				REQUIRE(p.ok());
				out.ray = std::max(out.ray, std::hypot(p.u - u, p.v - v));
			}
		}
		return out;
	}

	ModelError errorOf(const Report& report, const CameraModel& truth)
	{
		REQUIRE(report.model.has_value());
		return errorOf(*report.model, truth);
	}

	const HeldOutEvidence& heldOutOf(const Report& report)
	{
		REQUIRE(std::holds_alternative<HeldOutEvidence>(report.heldOut));
		return std::get<HeldOutEvidence>(report.heldOut);
	}

	const ResamplingEvidence& resamplingOf(const Report& report)
	{
		REQUIRE(std::holds_alternative<ResamplingEvidence>(report.resampling));
		return std::get<ResamplingEvidence>(report.resampling);
	}

	// The noise the stand-in states for every observation: sigma 0.034 of a feature's size of 4 px.
	constexpr double kStatedSigma = 0.136;
} // namespace

TEST_CASE("a pinhole camera is recovered from exact tracks, Ready", "[camera][ceres][targetless][calibration]")
{
	const lain::testing::ThreadPool pool{4};
	SECTION("its principal point at the centre")
	{
		const CameraModel truth = centred();
		setUp(truth);
		const Report report = run(scene::sequenceTrackSetOf());
		INFO(report.toString());
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		const TargetlessDiagnostics& t = targetlessOf(report);
		REQUIRE(t.swept.has_value());
		CHECK(t.sweep[*t.swept].focalLength < 500); // 465.96: the sweep starts 7% short
		REQUIRE(t.refinement.has_value());
		CHECK(t.refinement->status == refinement::Status::Converged);
		CHECK(report.reproducibility.estimator.backend == "ceres");
		CHECK(std::get<TargetlessRecord>(report.reproducibility.method).refiner.backend == "ceres");
		// Measured: focal 2.5e-11, the ray mapping 2.6e-7 px; held out 4.9e-8 mrad.
		const ModelError error = errorOf(report, truth);
		CHECK(error.focal < 1e-7);
		CHECK(error.ray < 1e-4);
		CHECK(heldOutOf(report).rmsAngle < 1e-8);
		CHECK(t.outliers.empty());
		CHECK(t.unplaced.empty());
		CHECK(report.verdict == Verdict::Ready);
	}
	SECTION("fx and fy apart, its principal point 1 px from the centre it is held at")
	{
		const CameraModel truth = cameraOf(500, 510, 320.5, 239.5);
		setUp(truth);
		targetless::Settings settings;
		settings.freePrincipalPoint = false; // held, for what a held miss does to the focal lengths
		const Report report = run(scene::sequenceTrackSetOf(), requestFor(DistortionModel::None), settings);
		INFO(report.toString());
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		REQUIRE(report.model.has_value());
		CHECK(report.model->intrinsics().cx == 319.5);
		// Measured: fx and fy within 1.8e-4 of their own; the ray mapping 1.06 px out, the held
		// principal point's 1 px miss.
		const ModelError error = errorOf(report, truth);
		CHECK(error.focal < 1e-3);
		CHECK(error.ray > 0.5);
		CHECK(error.ray < 1.5);
		CHECK(report.verdict == Verdict::Ready);
	}
}

TEST_CASE("a Brown-Conrady camera is recovered from exact tracks, Ready", "[camera][ceres][targetless][calibration]")
{
	const lain::testing::ThreadPool pool{4};
	// 24 frames and 200 landmarks: on 12 frames and 120, the start 7% short left 2 of 10 frames
	// unplaced, the stand-in's strictness rather than the method's (sub-slice 9c's plan).
	const CameraModel truth = cameraOf(500, 500, 319.5, 239.5, BrownConrady5{0.1, -0.05, 0.001, -0.002, 0.01});
	setUp(truth, 24, 200);
	const Report report = run(scene::sequenceTrackSetOf(), requestFor(DistortionModel::BrownConrady5));
	INFO(report.toString());
	REQUIRE(report.status == CalibrationStatus::Succeeded);
	REQUIRE(report.model.has_value());
	CHECK(modelOf(report.model->distortion()) == DistortionModel::BrownConrady5);
	// Measured: focal 3.0e-11, the ray mapping 4.1e-7 px.
	const ModelError error = errorOf(report, truth);
	CHECK(error.focal < 1e-7);
	CHECK(error.ray < 1e-4);
	CHECK(targetlessOf(report).unplaced.empty());
	CHECK(report.verdict == Verdict::Ready);
}

TEST_CASE("noise at the stated covariance calibrates to within it, Ready", "[camera][ceres][targetless][calibration]")
{
	const lain::testing::ThreadPool pool{4};
	const CameraModel truth = centred();
	SECTION("12 frames and 120 landmarks")
	{
		setUp(truth);
		const Report report = run(scene::sequenceTrackSetOf(kStatedSigma, 5));
		INFO(report.toString());
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		// Measured: the focal length 0.246% long, the principal point 0.13 px off, the ray mapping 1.11 px
		// out; held out 0.435 mrad; focal variation 0.106% and the principal point's 0.67 mrad; 6 of 1200
		// observations named, the noise's own share.
		const ModelError error = errorOf(report, truth);
		CHECK(error.focal < 0.005);
		CHECK(error.ray < 2.0);
		const HeldOutEvidence& held = heldOutOf(report);
		CHECK(held.rmsAngle < 0.001);
		CHECK(held.outliers == 0);
		const ResamplingEvidence& resampling = resamplingOf(report);
		CHECK(resampling.focalVariation > 0);
		CHECK(resampling.focalVariation < 0.003);
		REQUIRE(std::holds_alternative<double>(resampling.principalPointVariation));
		CHECK(std::get<double>(resampling.principalPointVariation) > 0);
		CHECK(std::get<double>(resampling.principalPointVariation) < 0.0015);
		const TargetlessDiagnostics& t = targetlessOf(report);
		REQUIRE(t.refinement.has_value());
		CHECK(t.outliers.size() * 100 < t.refinement->residuals);
		CHECK(report.thresholds.name == "reconstruction-targetless/1");
		CHECK(report.verdict == Verdict::Ready);
	}
	SECTION("24 frames and 300 landmarks")
	{
		setUp(truth, 24, 300);
		const Report report = run(scene::sequenceTrackSetOf(kStatedSigma, 5));
		INFO(report.toString());
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		// Measured: the focal length 0.049% out, the ray mapping 0.374 px, the principal point's 0.25 px
		// error included (0.135 px when it was held at the truth).
		const ModelError error = errorOf(report, truth);
		CHECK(error.focal < 0.001);
		CHECK(error.ray < 0.75);
		CHECK(report.verdict == Verdict::Ready);
	}
}

TEST_CASE("a wrong match is named by its track and frame, and neither the model nor the fit RMS moves", "[camera][ceres][targetless][calibration]")
{
	const lain::testing::ThreadPool pool{4};
	const CameraModel truth = centred();
	setUp(truth);
	const feature::TrackSet clean = scene::sequenceTrackSetOf(kStatedSigma, 5);
	feature::TrackSet wrong = clean;
	feature::SceneObservation& moved = wrong.tracks[7].observations[3];
	moved.pixel.x += 25;
	const Report before = run(clean);
	const Report after = run(wrong);
	INFO(after.toString());
	REQUIRE(before.status == CalibrationStatus::Succeeded);
	REQUIRE(after.status == CalibrationStatus::Succeeded);

	// The moved observation's frame is its view's, every view being one frame in capture order; and
	// it is not a held-out frame's, so the adjustment saw it.
	const std::uint32_t frame = moved.view;
	REQUIRE(std::count(after.diagnostics.heldOut.begin(), after.diagnostics.heldOut.end(), frame) == 0);
	const std::vector<Outlier>& outliers = targetlessOf(after).outliers;
	const auto named = std::find_if(outliers.begin(), outliers.end(), [&](const Outlier& o)
									{ return o.track == wrong.tracks[7].identity && o.frame == frame; });
	REQUIRE(named != outliers.end());
	CHECK(named->rmsWhitened > 100); // measured 182
	// Every other outlier is the noise's, which the clean run names too.
	CHECK(outliers.size() == targetlessOf(before).outliers.size() + 1);

	// The fit RMS is over the observations the result explains, so the named one, 25 px out, neither
	// counts nor moves it. Measured: 0.167625 px over 1194 clean, 0.167585 over 1193 with it.
	CHECK(after.diagnostics.fitResiduals + 1 == before.diagnostics.fitResiduals);
	CHECK(std::abs(after.diagnostics.fitRmsPixels - before.diagnostics.fitRmsPixels) < 1e-3);
	// Measured: the focal length moved by 2.8e-6 of itself, and the held-out angle by 7e-5 mrad.
	CHECK(std::abs(after.model->intrinsics().fx / before.model->intrinsics().fx - 1) < 1e-4);
	CHECK(std::abs(heldOutOf(after).rmsAngle - heldOutOf(before).rmsAngle) < 1e-6);
	CHECK(after.verdict == Verdict::Ready);
}

TEST_CASE("a resample leaves out what the refined model cannot project, and keeps its evidence",
		  "[camera][ceres][targetless][calibration]")
{
	const lain::testing::ThreadPool pool{4};
	// A camera whose distortion folds 44.8 degrees off its axis, its image's corner being 40.3: just
	// outside the image. 24 frames and 200 landmarks, as the Brown-Conrady case's.
	const CameraModel truth = cameraOf(500, 500, 319.5, 239.5, BrownConrady5{0, 0, 0, 0, -0.15});
	setUp(truth, 24, 200);
	// Eight landmarks about half a metre in front of the last frame, which the frames near the end see
	// and which lie 55 to 60 degrees off frame 0's axis, in front of it but beyond the fold; each is
	// matched wrongly in frame 0 too, at a pixel of its own. The adjustment starts from a neutral model,
	// whose domain is unbounded, so it fits those matches and names them; a resample starts from the
	// refined model, whose domain ends at the fold.
	const std::vector<math::Vec3d> offsets{{-0.4, 0.0, 0.4}, {-0.2, 0.0, 0.4}, {-0.4, -0.3, 0.5}, {-0.4, 0.0, 0.5}, {-0.2, -0.3, 0.5}, {-0.2, 0.0, 0.5}, {0.0, 0.0, 0.5}, {-0.4, -0.3, 0.6}};
	const math::RigidTransformd& last = scene::scene().path.back();
	const math::RigidTransformd frame0FromReference = scene::scene().path.front().inverse();
	for (std::size_t i = 0; i < offsets.size(); ++i)
	{
		scene::Landmark landmark;
		landmark.point = last.apply(offsets[i]);
		landmark.descriptor = std::uint32_t(1000000 + 1000 * i);
		const math::Vec3d q = frame0FromReference.apply(landmark.point);
		REQUIRE(q.z > 0);
		REQUIRE(project(truth, q.x, q.y, q.z).status == ProjectionStatus::OutsideDomain);
		scene::scene().landmarks.push_back(landmark);
	}
	feature::TrackSet set = scene::sequenceTrackSetOf(kStatedSigma, 5);
	std::vector<std::string> wrong;
	for (feature::Track& track : set.tracks)
	{
		if (std::stoi(track.identity.substr(5)) < 200)
			continue;
		REQUIRE(track.observations.size() >= 3); // measured: 3 to 8 frames see each
		feature::SceneObservation match = track.observations.front();
		match.view = 0;
		match.pixel = math::Vec2d{100.0 + 50.0 * double(wrong.size()), 300.0};
		track.observations.push_back(match);
		wrong.push_back(track.identity);
	}
	REQUIRE(wrong.size() == offsets.size());

	const Report report = run(set, requestFor(DistortionModel::BrownConrady5));
	INFO(report.toString());
	REQUIRE(report.status == CalibrationStatus::Succeeded);
	REQUIRE(std::count(report.diagnostics.heldOut.begin(), report.diagnostics.heldOut.end(), 0u) == 0);
	// The adjustment's start left nothing out, so it fitted every wrong match; the refined model cannot
	// project one, so each is named with an infinite residual.
	const TargetlessDiagnostics& t = targetlessOf(report);
	CHECK(t.behind == 0);
	for (const std::string& track : wrong)
	{
		CAPTURE(track);
		CHECK(std::any_of(t.outliers.begin(), t.outliers.end(), [&](const Outlier& o)
						  { return o.track == track && o.frame == 0 && std::isinf(o.rmsWhitened); }));
	}
	// Measured: every resample produced a model. A resample keeping every observation the adjustment
	// fitted failed at its first evaluation whenever it drew one of these landmarks, none produced a
	// model, and the run was Exploratory for want of resampling evidence.
	CHECK(resamplingOf(report).resamples == report.reproducibility.request.resamples);
	CHECK(report.verdict == Verdict::Ready);
}

TEST_CASE("moving content is named, and the model does not move", "[camera][ceres][targetless][calibration]")
{
	const lain::testing::ThreadPool pool{4};
	const CameraModel truth = centred();
	setUp(truth);
	std::vector<std::string> moving;
	for (std::size_t l = 0; l < 120; l += 10)
	{
		scene::scene().landmarks[l].dynamic = true;
		moving.push_back("track" + std::string(5 - std::to_string(l).size(), '0') + std::to_string(l));
	}
	const feature::TrackSet set = scene::sequenceTrackSetOf(kStatedSigma, 5);
	const Report report = run(set);
	INFO(report.toString());
	REQUIRE(report.status == CalibrationStatus::Succeeded);

	// Each moving track is named in most of the frames the adjustment saw it in (measured: at 19.8 to
	// 495 sigma), and what else is named is the noise's share.
	std::map<std::string, std::size_t> namedIn, fittedIn;
	for (const Outlier& o : targetlessOf(report).outliers)
		++namedIn[o.track];
	for (const feature::Track& track : set.tracks)
	{
		for (const feature::SceneObservation& o : track.observations)
		{
			if (std::count(report.diagnostics.heldOut.begin(), report.diagnostics.heldOut.end(), o.view) == 0)
				++fittedIn[track.identity];
		}
	}
	std::size_t others = 0;
	for (const auto& [track, count] : namedIn)
	{
		if (std::count(moving.begin(), moving.end(), track) == 0)
			others += count;
	}
	for (const std::string& track : moving)
	{
		CAPTURE(track, namedIn[track], fittedIn[track]);
		CHECK(2 * namedIn[track] > fittedIn[track]);
	}
	CHECK(others < 12); // 1% of the static observations

	// Measured: the ray mapping 0.85 px out; the held-out frames count 18 observations beyond their
	// inlier angle and measure 0.846 mrad over the other 219.
	const ModelError error = errorOf(report, truth);
	CHECK(error.ray < 2.0);
	const HeldOutEvidence& held = heldOutOf(report);
	CHECK(held.outliers > 0);
	CHECK(held.rmsAngle < 0.0015);
	CHECK(report.verdict == Verdict::Ready);
}

TEST_CASE("a principal point (8, -6) px off is recovered by default, and missed when held",
		  "[camera][ceres][targetless][calibration]")
{
	const lain::testing::ThreadPool pool{4};
	const CameraModel truth = cameraOf(500, 500, 327.5, 233.5);
	setUp(truth);
	const feature::TrackSet set = scene::sequenceTrackSetOf();
	SECTION("freed, by default")
	{
		const Report report = run(set);
		INFO(report.toString());
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		// Measured: the principal point exactly where the truth's is, the ray mapping 2.3e-6 px out.
		CHECK(std::abs(report.model->intrinsics().cx - 327.5) < 1e-4);
		CHECK(std::abs(report.model->intrinsics().cy - 233.5) < 1e-4);
		CHECK(errorOf(report, truth).ray < 1e-4);
		CHECK(std::holds_alternative<double>(resamplingOf(report).principalPointVariation));
		CHECK(report.verdict == Verdict::Ready);
	}
	SECTION("held, as the settings say")
	{
		targetless::Settings settings;
		settings.freePrincipalPoint = false;
		const Report report = run(set, requestFor(DistortionModel::None), settings);
		INFO(report.toString());
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		CHECK(report.model->intrinsics().cx == 319.5);
		CHECK(report.model->intrinsics().cy == 239.5);
		CHECK(std::holds_alternative<Held>(resamplingOf(report).principalPointVariation));
		// Measured: the ray mapping 10.7 px out, and held out 2.17 mrad, past Ready's 1.5. On exact tracks
		// a held miss this large shows; on rendered footage, under noise, the held-out frames' poses
		// absorbed one of 10 to 18 px (1.1 to 1.4 mrad) and the model was Ready, which is why the
		// principal point is freed by default (sub-slice 10's probe).
		CHECK(errorOf(report, truth).ray > 8.0);
		CHECK(heldOutOf(report).rmsAngle > 0.0015);
		CHECK(report.verdict == Verdict::Exploratory);
	}
}

TEST_CASE("targetless calibration through Ceres is the same in parallel and serially, resampling included",
		  "[camera][ceres][targetless][calibration]")
{
	setUp(centred());
	const feature::TrackSet set = scene::sequenceTrackSetOf(kStatedSigma, 5);
	Request serial = requestFor(DistortionModel::None);
	serial.execution = ExecutionPolicy::DeterministicDebug;
	const Report a = run(set, serial);
	REQUIRE(a.status == CalibrationStatus::Succeeded);
	CHECK(resamplingOf(a).focalVariation > 0); // noise, so the resamples differ

	const lain::testing::ThreadPool pool{4};
	requireSameReport(a, run(set));
}

TEST_CASE("tracks named to sort another way give one report", "[camera][ceres][targetless][calibration]")
{
	const lain::testing::ThreadPool pool{4};
	// A track's identity digests its frames' source, so footage at another path renames every track:
	// this is that case, with the names reversed so their order is the opposite of capture order.
	setUp(centred());
	const feature::TrackSet set = scene::sequenceTrackSetOf(kStatedSigma, 5);
	feature::TrackSet renamed = set;
	std::map<std::string, std::string> original;
	for (feature::Track& track : renamed.tracks)
	{
		const std::string name = "track" + std::to_string(99999 - std::stoi(track.identity.substr(5)));
		original[name] = track.identity;
		track.identity = name;
	}
	const Report a = run(set);
	Report b = run(renamed);
	REQUIRE(a.status == CalibrationStatus::Succeeded);
	CHECK(resamplingOf(a).focalVariation > 0); // noise, so the resamples differ
	for (Outlier& o : std::get<TargetlessDiagnostics>(b.diagnostics.method).outliers)
		o.track = original.at(o.track);
	requireSameReport(a, b);
}
