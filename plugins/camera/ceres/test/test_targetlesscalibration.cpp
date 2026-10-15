// Targetless calibration (calibration::targetless) through the Ceres refiner, over the stand-in scene
// and geometry (syntheticscene.h): one moving camera's model recovered from its frames' tracks, exact
// and noisy; a wrong match and moving content named and kept from moving the model; a principal point
// freed and held; and one report however the work ran or the tracks are named. The stand-in geometry
// proposes only true poses, so it cannot place a frame a starting model misses by much, which is why
// the principal point is 3 px off rather than more (WORK.md, sub-slice 9c's plan). A model is compared
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
	SECTION("its principal point at the centre, where it is held")
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
		// Measured: focal 3.6e-9, the ray mapping 1.4e-6 px; held out 4.7e-7 mrad.
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
		const Report report = run(scene::sequenceTrackSetOf());
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
	// Measured: focal 8.3e-10, the ray mapping 5.1e-7 px.
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
		// Measured: the focal length 0.245% long, the ray mapping 0.96 px out; held out 0.434 mrad;
		// focal variation 0.105%; 6 of 1200 observations named, the noise's own share.
		const ModelError error = errorOf(report, truth);
		CHECK(error.focal < 0.005);
		CHECK(error.ray < 2.0);
		const HeldOutEvidence& held = heldOutOf(report);
		CHECK(held.rmsAngle < 0.001);
		CHECK(held.outliers == 0);
		const ResamplingEvidence& resampling = resamplingOf(report);
		CHECK(resampling.focalVariation > 0);
		CHECK(resampling.focalVariation < 0.003);
		CHECK(std::holds_alternative<Held>(resampling.principalPointVariation));
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
		// Measured: the focal length 0.048% out, the ray mapping 0.135 px.
		const ModelError error = errorOf(report, truth);
		CHECK(error.focal < 0.001);
		CHECK(error.ray < 0.3);
		CHECK(report.verdict == Verdict::Ready);
	}
}

TEST_CASE("a wrong match is named by its track and frame, and the model does not move", "[camera][ceres][targetless][calibration]")
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

	// Measured: the focal length moved by 2.8e-6 of itself, and the held-out angle by 7e-5 mrad.
	CHECK(std::abs(after.model->intrinsics().fx / before.model->intrinsics().fx - 1) < 1e-4);
	CHECK(std::abs(heldOutOf(after).rmsAngle - heldOutOf(before).rmsAngle) < 1e-6);
	CHECK(after.verdict == Verdict::Ready);
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

	// Measured: the ray mapping 0.30 px out; the held-out frames count 18 observations beyond their
	// inlier angle and measure 0.842 mrad over the other 219.
	const ModelError error = errorOf(report, truth);
	CHECK(error.ray < 1.0);
	const HeldOutEvidence& held = heldOutOf(report);
	CHECK(held.outliers > 0);
	CHECK(held.rmsAngle < 0.0015);
	CHECK(report.verdict == Verdict::Ready);
}

TEST_CASE("a principal point 3 px off is recovered when freed, and missed when held", "[camera][ceres][targetless][calibration]")
{
	const lain::testing::ThreadPool pool{4};
	const CameraModel truth = cameraOf(500, 500, 322.5, 241.5);
	setUp(truth);
	const feature::TrackSet set = scene::sequenceTrackSetOf();
	SECTION("freed")
	{
		targetless::Settings settings;
		settings.freePrincipalPoint = true;
		const Report report = run(set, requestFor(DistortionModel::None), settings);
		INFO(report.toString());
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		// Measured: the principal point exactly where the truth's is, the ray mapping 6e-7 px out.
		CHECK(std::abs(report.model->intrinsics().cx - 322.5) < 1e-4);
		CHECK(std::abs(report.model->intrinsics().cy - 241.5) < 1e-4);
		CHECK(errorOf(report, truth).ray < 1e-4);
		CHECK(std::holds_alternative<double>(resamplingOf(report).principalPointVariation));
		CHECK(report.verdict == Verdict::Ready);
	}
	SECTION("held")
	{
		const Report report = run(set);
		INFO(report.toString());
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		CHECK(report.model->intrinsics().cx == 319.5);
		CHECK(report.model->intrinsics().cy == 239.5);
		CHECK(std::holds_alternative<Held>(resamplingOf(report).principalPointVariation));
		// What the profile cannot see. Measured: the ray mapping 3.87 px out, held out 0.805 mrad
		// against Ready's 1.5, since the frames' poses absorb the offset.
		CHECK(errorOf(report, truth).ray > 3.0);
		CHECK(heldOutOf(report).rmsAngle > 0.0005);
		CHECK(report.verdict == Verdict::Ready);
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
