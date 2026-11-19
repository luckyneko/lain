// The rig's feature tracks measured against their truth, for the thresholds M9's slice 3 left
// provisional: how many wrong tracks verify, how far the right ones sit from the truth, whether
// counting an epipolar residual twice is right, and the localisation model size band by size band.
// These are the recorded evidence for localisationPerSize = 0.034, the epipolar weight and no floor
// on the stated sigma (WORK.md, sub-slice 9).
//
// On footage drawn per ray by texturedscene.h: four fixed cameras over a textured scene with a printed
// board moving through it, 503 tracks. It stays there rather than moving onto the committed
// two-methods rig plan with the comparison of both methods (test-camera-comparison): on that capture
// the plan gives about 270 tracks, and with so few an RMS is decided by the handful just under 10
// mrad, so the band and epipolar numbers swing between devices (band 4-8 px 0.86 on a hardware GPU,
// 2.31 on the CPU device; epipolar ratio 0.98 against 2.19), and no longer measure the model. The
// per-ray render is also the exact instrument for localisation: no rasteriser, no 8-bit texture store.
//
// Rendered at 1280x720 with a focal length of 1000, so it runs in Release only: a Debug build draws a
// frame in seconds.

#include "registration.h"
#include "renderedrig.h"

#include <lain/camera/board/rendering.h>
#include <lain/camera/feature/extraction.h>
#include <lain/camera/projection.h>
#include <lain/camera/registration/validation.h>
#include <lain/camera/testing/rigcompare.h>
#include <lain/camera/testing/testboard.h>
#include <lain/camera/testing/testcamera.h>
#include <lain/testing/skip.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace lain;
using namespace lain::camera;
namespace compare = lain::camera::testing;

const double kTau = math::two_pi<double>();
constexpr std::size_t kFrames = 16;
constexpr double kFocal = 1000.0;

static CameraModel hdCamera()
{
	CameraModelParameters p;
	p.image = {1280, 720};
	p.intrinsics = {kFocal, 1002.0, 641.3, 359.2};
	p.distortion = camera::testing::brownConrady();
	ModelResult result = CameraModel::create(p);
	REQUIRE(result.model.has_value());
	return *result.model;
}

// The rig's footage: four cameras 0.6 m apart on the default scene's arc, and the board sweeping
// a Lissajous 1.6 m in front of them, turning as it goes, so every corner moves well between any
// two sampled frames and no part of the scene stays covered in most of them.
struct Footage
{
	rendered::Rig rig;
	board::Specification spec;
	std::vector<math::RigidTransformd> boardPoses; // worldFromBoard, per frame
};

static Footage footage()
{
	fixture::ensureRegistered();
	const board::Specification spec = camera::testing::specification(camera::testing::pattern(), 80.0);
	const board::RenderResult rendered = board::render(spec.pattern(), board::RenderRequest{120, 30});
	REQUIRE(rendered.rendering.has_value());

	std::vector<math::RigidTransformd> cameras;
	math::Vec3d middle{0.0};
	for (const double angle : {-0.28, -0.094, 0.094, 0.28})
	{
		cameras.push_back(synthetic::arcCamera(angle));
		middle += cameras.back().translation() / 4.0;
	}
	const math::Vec3d target{-0.2, 0.2, 0.4};
	const math::Vec3d ahead = math::normalize(target - middle);
	const math::Vec3d centre = middle + 1.6 * ahead;
	// The board's axes when square on: X right, Y down, Z away from the rig.
	const math::RigidTransformd facing = math::lookingAt(centre, centre + ahead, kCameraFrame).value();
	const board::PatternParameters& p = spec.pattern().parameters();
	const double square = spec.instance().squareLength.value.metres();
	const math::Vec3d boardMiddle{p.squaresX * square / 2, p.squaresY * square / 2, 0.0};
	std::vector<math::RigidTransformd> poses;
	for (std::size_t i = 0; i < kFrames; ++i)
	{
		const double t = (double(i) + 0.5) / double(kFrames);
		const math::Vec3d offset{0.3 * std::sin(kTau * t), 0.15 * std::sin(2 * kTau * t + 0.7), 0.0};
		const math::Quatd turn = facing.rotation() * math::angleAxis(0.35 * std::sin(2 * kTau * t), math::Vec3d{0, 1, 0}) *
								 math::angleAxis(0.25 * std::cos(3 * kTau * t), math::Vec3d{1, 0, 0});
		const math::Vec3d at = centre + facing.rotate(offset);
		poses.push_back(math::RigidTransformd{turn, at - turn * boardMiddle});
	}

	synthetic::Scene scene = synthetic::defaultScene();
	scene.noise = 1.0;
	scene.board =
		synthetic::boardSurface(std::make_shared<const board::Rendering>(*rendered.rendering), square, poses);
	return Footage{rendered::rig(scene, hdCamera(), cameras, kFrames, 3), spec, poses};
}

static registration::HeldOutEvidence evidence(const std::variant<registration::HeldOutEvidence, camera::Unavailable>& v)
{
	const auto* e = std::get_if<registration::HeldOutEvidence>(&v);
	if (!e)
		FAIL("validation unavailable: " << std::get<camera::Unavailable>(v).reason);
	return *e;
}

// One track judged against the true rig: its median member's size and stated localisation, and
// how far the truth predicts each member from where it was found.
struct TrackTruth
{
	std::string id;
	std::size_t members = 0;
	double size = 0;	 // the median member's keypoint size, in processed pixels
	double sigma = 0;	 // the median member's stated localisation, radians at the focal length
	double residual = 0; // RMS transfer against the true rig, radians
};

static std::vector<TrackTruth> truthOf(const std::vector<registration::RigCamera>& cameras, const registration::Report& truth,
									   const feature::ExtractionResult& extracted)
{
	std::vector<TrackTruth> out;
	const double scale = extracted.report.cameras.front().scale;
	for (const feature::Track& track : extracted.trackSet.tracks)
	{
		TrackTruth t;
		t.id = track.identity;
		t.members = track.observations.size();
		std::vector<double> sigmas;
		for (const feature::SceneObservation& o : track.observations)
			sigmas.push_back(std::sqrt((*o.covariance)[0]));
		std::sort(sigmas.begin(), sigmas.end());
		const double sigma = sigmas[sigmas.size() / 2]; // source pixels
		t.size = sigma / extracted.report.request.localisationPerSize * scale;
		t.sigma = sigma / kFocal;
		t.residual = evidence(registration::validate(cameras, truth, extracted.trackSet, {t.id})).rmsAngle;
		out.push_back(t);
	}
	return out;
}

static double rmsOver(const std::vector<registration::RigCamera>& cameras, const registration::Report& truth,
					  const feature::TrackSet& set, const std::vector<std::string>& ids)
{
	return ids.empty() ? 0.0 : evidence(registration::validate(cameras, truth, set, ids)).rmsAngle;
}

TEST_CASE("the rig's feature tracks measured against their truth", "[camera][registration][comparison]")
{
	lain::testing::skipInDebug("the comparison runs in Release builds: a Debug build takes seconds to draw each HD frame");
	const lain::testing::ThreadPool pool;
	const Footage f = footage();
	const rendered::Rig& rig = f.rig;
	const std::vector<registration::RigCamera> cameras = rendered::camerasOf(rig);
	// The truth scored as a registration, so validate measures the tracks alone.
	const registration::Report truth = compare::truthReport(rig.identities, compare::rebased(rig.worldFromCamera, 1));

	// Measured, the default extraction (native search, Exact): 503 tracks in 20.6 s, 1.47 descriptor
	// rows per feature, 2 features dropped as ambiguous, 3 conflicts rejected. Approximate search:
	// the same 503 tracks in 1.86 s. Half scale: 464 tracks in 1.22 s.
	const feature::ExtractionResult native = feature::extractTracks(rig.footage, rig.groups, feature::ExtractionRequest{});
	REQUIRE(native.ok());
	std::uint64_t found = 0, orientations = 0;
	for (const feature::CameraExtraction& c : native.report.cameras)
	{
		found += c.found;
		orientations += c.orientations;
	}
	const double rowsPerFeature = double(found) / double(found - orientations);
	CHECK(rowsPerFeature > 1.3);
	CHECK(rowsPerFeature < 1.6);
	// The feature cap is not what limits these frames: measured, about 1100 features a frame (1620
	// rows), against a cap of 8192.
	for (const feature::CameraExtraction& c : native.report.cameras)
		CHECK(c.capped == 0);
	feature::ExtractionRequest approximate;
	approximate.matching.search = feature::MatchSearch::Approximate;
	const feature::ExtractionResult fast = feature::extractTracks(rig.footage, rig.groups, approximate);
	REQUIRE(fast.ok());
	CHECK(double(fast.trackSet.tracks.size()) > 0.95 * double(native.trackSet.tracks.size()));
	feature::ExtractionRequest half;
	half.scale = ScaleFactor{0.5};
	const feature::ExtractionResult coarse = feature::extractTracks(rig.footage, rig.groups, half);
	REQUIRE(coarse.ok());

	for (const feature::ExtractionResult* extracted : {&native, &coarse})
	{
		CAPTURE(extracted->report.cameras.front().scale);
		const std::vector<TrackTruth> tracks = truthOf(cameras, truth, *extracted);
		std::vector<std::string> good, pairs, longer;
		for (const TrackTruth& track : tracks)
		{
			if (track.residual > 0.01)
				continue;
			good.push_back(track.id);
			(track.members == 2 ? pairs : longer).push_back(track.id);
		}

		// Wrong tracks that verified: more than 10 mrad from where the truth puts them. Measured: 2 of
		// 503 natively, one 275 mrad out on a 2 px feature and one 13 mrad out on a 42 px one; 1 of 464
		// at half scale. Everything else: a median of 0.16 mrad and a 95th percentile of 2.1. The
		// wrong ones are why validate over every track reads 15.3 mrad natively: its RMS has no robust
		// loss, so one wrong track among the held-out ones would fail Ready's 2 mrad on its own
		// (WORK.md, sub-slice 9).
		const std::size_t wrong = tracks.size() - good.size();
		CHECK(wrong * 100 <= tracks.size());
		CHECK(rmsOver(cameras, truth, extracted->trackSet, good) < 0.0012);

		// The epipolar doubling: two-member tracks against longer ones, both against the truth.
		// Measured: 0.98 against 0.78 mrad natively (287 and 214 tracks), 1.18 against 0.95 at half
		// scale, a ratio of 1.25 both times. Counting an epipolar residual twice over-counts a little,
		// as expected: it carries both members' noise out of the plane, where a transfer residual
		// carries one member's and a landmark fixed by two or more others.
		const double ratio = rmsOver(cameras, truth, extracted->trackSet, pairs) /
							 rmsOver(cameras, truth, extracted->trackSet, longer);
		CHECK(ratio > 1.0);
		CHECK(ratio < 1.5);

		// The localisation model, size band by size band: each band's RMS against its RMS stated sigma,
		// on the tracks that are right. Measured, natively: 1.25, 1.50 and 1.40 for 4-8, 8-16 and over 16
		// processed px, about the root two a transfer between two equally sure members should show;
		// and 13 for features under 4 px (a median of 2.3 times the stated sigma, against 0.2 above),
		// which is SIFT's doubled first octave stating 0.1 px where it localises to about 1. At half
		// scale: 1.65, 1.80, 2.33 and 1.39, small features included. A floor on the stated sigma did not
		// help: on this footage 0.25 px left the rig where it was and 0.5 px moved it from 0.74 to
		// 1.31 mrad, since it takes weight from the features that are right (WORK.md, sub-slice 9).
		for (const auto& [low, high] : {std::pair<double, double>{4, 8}, {8, 16}, {16, 1e9}})
		{
			std::vector<std::string> ids;
			double sigmas = 0;
			for (const TrackTruth& track : tracks)
			{
				if (track.residual > 0.01 || track.size < low || track.size >= high)
					continue;
				ids.push_back(track.id);
				sigmas += track.sigma * track.sigma;
			}
			CAPTURE(low);
			REQUIRE(ids.size() > 20);
			const double stated = std::sqrt(sigmas / double(ids.size()));
			const double measured = rmsOver(cameras, truth, extracted->trackSet, ids) / stated;
			CHECK(measured > 1.0);
			CHECK(measured < 2.6);
		}
	}
}
