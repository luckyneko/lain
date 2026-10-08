// One rig registered by both methods from the same footage, through both real backends: four fixed
// cameras over a textured scene with a printed board moving through it. Board registration finds the
// board and places the cameras metrically; targetless registration finds the scene, drops the board
// as moving content, and places them up to scale. Both results are validated on the same held-out
// feature tracks (registration::validate), compared with each other and with the truth, and the
// rig's tracks are measured against the truth for the thresholds slice 3 left provisional.
//
// Rendered at 1280x720 with a focal length of 1000, the operating point registration-targetless/1's
// angles were set for, so these cases run in Release only: a Debug build draws a frame in seconds.

#include "registration.h"
#include "renderedrig.h"
#include "rigcompare.h"
#include "testboard.h"
#include "testcamera.h"

#include <lain/camera/board/rendering.h>
#include <lain/camera/feature/extraction.h>
#include <lain/camera/projection.h>
#include <lain/camera/registration/board.h>
#include <lain/camera/registration/targetless.h>
#include <lain/camera/registration/validation.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

using namespace lain;
using namespace lain::camera;
namespace compare = lain::camera::testing;

namespace
{
	constexpr double kTau = 6.283185307179586;
	constexpr std::size_t kFrames = 16;
	constexpr double kFocal = 1000.0;

	CameraModel hdCamera()
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

	Footage footage()
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
		const math::RigidTransformd facing = synthetic::lookingAt(centre, centre + ahead);
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

	registration::HeldOutEvidence evidence(const std::variant<registration::HeldOutEvidence, camera::Unavailable>& v)
	{
		const auto* e = std::get_if<registration::HeldOutEvidence>(&v);
		if (!e)
			FAIL("validation unavailable: " << std::get<camera::Unavailable>(v).reason);
		return *e;
	}

	double perCamera(const registration::HeldOutEvidence& e, const capture::CameraIdentity& camera)
	{
		for (const auto& [id, angle] : e.perCamera)
		{
			if (id == camera)
				return angle;
		}
		FAIL("no evidence for " << camera.value);
		return 0;
	}

	// Of `ids`, those whose track has `members` observations (or more, with `orMore`).
	std::vector<std::string> withMembers(const feature::TrackSet& set, const std::vector<std::string>& ids,
										 std::size_t members, bool orMore)
	{
		std::set<std::string> wanted(ids.begin(), ids.end());
		std::vector<std::string> out;
		for (const feature::Track& t : set.tracks)
		{
			if (wanted.count(t.identity) == 0)
				continue;
			const std::size_t n = t.observations.size();
			if (n == members || (orMore && n > members))
				out.push_back(t.identity);
		}
		return out;
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

	std::vector<TrackTruth> truthOf(const std::vector<registration::RigCamera>& cameras, const registration::Report& truth,
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

	double rmsOver(const std::vector<registration::RigCamera>& cameras, const registration::Report& truth,
				   const feature::TrackSet& set, const std::vector<std::string>& ids)
	{
		return ids.empty() ? 0.0 : evidence(registration::validate(cameras, truth, set, ids)).rmsAngle;
	}
} // namespace

TEST_CASE("one rig registered with a board and without one agrees with itself and its truth",
		  "[camera][registration][comparison]")
{
#ifndef NDEBUG
	SKIP("the comparison runs in Release builds: a Debug build takes seconds to draw each HD frame");
#endif
	const lain::testing::ThreadPool pool;
	const Footage f = footage();
	const rendered::Rig& rig = f.rig;
	const std::vector<registration::RigCamera> cameras = rendered::camerasOf(rig);

	// Both methods over the same footage and capture groups, relative to the same camera. Measured:
	// board registration 56.7 s, nearly all of it drawing the 64 frames it detects in; targetless
	// 3.3 s on the 20 frames it samples, already drawn.
	registration::Request request;
	request.reference = rig.identities[1];
	const registration::Report board =
		registration::board::registerCameras(rig.footage, rig.groups, f.spec, camera::board::DetectionRequest{}, request);
	INFO(board.toString());
	REQUIRE(board.status == registration::RegistrationStatus::Succeeded);
	const registration::Report targetless =
		registration::targetless::registerCameras(rig.footage, rig.groups, feature::ExtractionRequest{}, request);
	INFO(targetless.toString());
	REQUIRE(targetless.status == registration::RegistrationStatus::Succeeded);
	const registration::TargetlessDiagnostics& t = std::get<registration::TargetlessDiagnostics>(targetless.diagnostics.method);
	REQUIRE(t.trackSet.has_value());
	const feature::TrackSet& tracks = *t.trackSet;

	// 1. What the footage must be for the comparison to mean anything.
	// Every camera sees the board in nearly every frame, so board registration has the capture groups
	// its Ready needs. Measured: all 16, in every camera.
	const registration::BoardDiagnostics& b = std::get<registration::BoardDiagnostics>(board.diagnostics.method);
	for (const capture::CameraIdentity& id : rig.identities)
	{
		std::size_t seen = 0;
		for (const registration::GroupDetections& g : b.detections)
		{
			for (const auto& [camera, report] : g.members)
				seen += camera == id && report.observation.has_value() ? 1 : 0;
		}
		CAPTURE(id.value);
		CHECK(seen >= 14);
	}
	// The board moves well between any two sampled frames in every camera, so none of its features
	// can pass for static: no repeated pose, no slow drift, no corner held still on a camera's line of
	// sight. Measured: every visible corner at least 21.7 px from where it was.
	double slowest = 1e9;
	for (std::size_t c = 0; c < rig.identities.size(); ++c)
	{
		const std::vector<media::FrameRef>& sampled = tracks.views[c].frames;
		for (std::size_t i = 0; i < sampled.size(); ++i)
		{
			for (std::size_t j = i + 1; j < sampled.size(); ++j)
			{
				for (std::uint32_t id = 0; id < f.spec.pattern().cornerCount(); ++id)
				{
					const math::Vec3d corner = *f.spec.cornerPosition(id);
					const auto a = rendered::projectInto(rig.model, rig.worldFromCamera[c], f.boardPoses[sampled[i].ordinal].apply(corner));
					const auto z = rendered::projectInto(rig.model, rig.worldFromCamera[c], f.boardPoses[sampled[j].ordinal].apply(corner));
					if (a && z)
						slowest = std::min(slowest, math::length(*a - *z));
				}
			}
		}
	}
	CHECK(slowest > 10.0);
	// What the board hides in most of the sampled frames, where no static feature can come from.
	// Measured: at most 5.3% of an image.
	double hidden = 0;
	for (std::size_t c = 0; c < rig.identities.size(); ++c)
	{
		std::size_t points = 0, covered = 0;
		for (int y = 8; y < 720; y += 16)
		{
			for (int x = 8; x < 1280; x += 16)
			{
				std::size_t on = 0;
				for (const media::FrameRef& frame : tracks.views[c].frames)
				{
					const std::optional<synthetic::Hit> hit =
						synthetic::truthAt(rig.scene, rig.model, rig.worldFromCamera[c], math::Vec2d{double(x), double(y)}, frame.ordinal);
					on += hit && hit->surface == synthetic::kBoardSurface ? 1 : 0;
				}
				covered += on * 2 > tracks.views[c].frames.size() ? 1 : 0;
				++points;
			}
		}
		hidden = std::max(hidden, double(covered) / double(points));
	}
	CHECK(hidden < 0.1);

	// 2. Each method against the truth, and the two against each other, in the truth's metres.
	// Measured: the board result 0.34 mrad and 0.61 mm from the truth with no scale fitted (a fitted
	// scale of 1.00004); the targetless result 0.74 mrad and 1.8 mm, once scaled; the two 0.55 mrad
	// and 1.3 mm apart, the board's scale making the targetless result metric (one normalised unit is
	// 4.18 m). The cameras stand 3.2 m from the scene and 0.6 m apart, the board 1.6 m away.
	const std::vector<math::RigidTransformd> truth = compare::rebased(rig.worldFromCamera, 1);
	const std::vector<math::RigidTransformd> byBoard = compare::posesOf(board, rig.identities);
	const std::vector<math::RigidTransformd> byScene = compare::posesOf(targetless, rig.identities);
	const compare::RigDifference boardTruth = compare::compare(truth, byBoard);
	double unscaled = 0;
	for (std::size_t c = 0; c < truth.size(); ++c)
		unscaled = std::max(unscaled, math::length(byBoard[c].translation() - truth[c].translation()));
	CHECK(std::abs(boardTruth.scale - 1.0) < 0.001);
	CHECK(boardTruth.worstRotation < 0.001);
	CHECK(unscaled < 0.002);
	const compare::RigDifference sceneTruth = compare::compare(truth, byScene);
	CHECK(sceneTruth.worstRotation < 0.0015);
	CHECK(sceneTruth.worstCentre < 0.004);
	const compare::RigDifference between = compare::compare(byBoard, byScene);
	CHECK(between.worstRotation < 0.0015);
	CHECK(between.worstCentre < 0.004);

	// 3. Both validated on the tracks the targetless registration held out: the same predictions of
	// the same members, so the two numbers compare one method with the other. Measured: 101 tracks,
	// 259 members (110 of them scored against the epipolar plane); 0.70 mrad for the board's result,
	// 0.72 for the targetless one, though the board's is twice as close to the truth. Held-out
	// transfer sees what the tracks can see: a camera turned (below), not the rig's weak mode, which
	// is what the bootstrap's rotation spread is for.
	const std::vector<std::string>& held = targetless.diagnostics.heldOutUnits;
	const registration::HeldOutEvidence onBoard = evidence(registration::validate(cameras, board, tracks, held));
	const registration::HeldOutEvidence onScene = evidence(registration::validate(cameras, targetless, tracks, held));
	CHECK(onBoard.units == held.size());
	CHECK(onBoard.units == onScene.units);
	CHECK(onBoard.predictions == onScene.predictions);
	CHECK(onBoard.rmsAngle < 0.002);
	CHECK(onScene.rmsAngle < 0.002);
	// The method's own evidence is this same call, on the same tracks.
	const registration::HeldOutEvidence& own = std::get<registration::HeldOutEvidence>(targetless.heldOut);
	CHECK(own.rmsAngle == onScene.rmsAngle);
	CHECK(own.predictions == onScene.predictions);

	// 4. The guard: one camera of the board's result turned 5 mrad about its own centre. Its
	// baselines all lie in a near-horizontal plane, so a pan keeps its rays in their epipolar planes,
	// where a two-member track cannot see it, and a tilt takes them out. Measured, cam02's transfer
	// and the rig's RMS from 0.58 and 0.70 mrad:
	// - pan: cam02 3.78 mrad, the rig 4.51; in two-member tracks 0.53 -> 0.71, in longer ones
	//   0.62 -> 5.07;
	// - tilt: cam02 5.75 mrad, the rig 4.12; in two-member tracks 0.53 -> 6.63, in longer ones
	//   0.62 -> 4.90.
	// Either way the rig's RMS crosses Ready's 2 mrad, but only because it has four cameras: fitness
	// judges the rig-wide RMS, so one such camera among many would pass (WORK.md, sub-slice 9).
	const std::vector<std::string> pairs = withMembers(tracks, held, 2, false);
	const std::vector<std::string> longer = withMembers(tracks, held, 3, true);
	for (const bool tilt : {false, true})
	{
		CAPTURE(tilt);
		registration::Report turned = board;
		for (registration::RegisteredCamera& c : turned.cameras)
		{
			if (c.camera == rig.identities[2])
				c.referenceFromCamera = c.referenceFromCamera *
										math::RigidTransformd{math::angleAxis(0.005, tilt ? math::Vec3d{1, 0, 0} : math::Vec3d{0, 1, 0}),
															  math::Vec3d{0.0}};
		}
		const registration::HeldOutEvidence all = evidence(registration::validate(cameras, turned, tracks, held));
		const registration::HeldOutEvidence two = evidence(registration::validate(cameras, turned, tracks, pairs));
		const registration::HeldOutEvidence more = evidence(registration::validate(cameras, turned, tracks, longer));
		CHECK(perCamera(all, rig.identities[2]) > (tilt ? 4.5e-3 : 3e-3));
		CHECK(all.rmsAngle > 0.002);
		CHECK(perCamera(more, rig.identities[2]) > 4e-3);
		if (tilt)
			CHECK(perCamera(two, rig.identities[2]) > 5e-3);
		else
			CHECK(perCamera(two, rig.identities[2]) < 1e-3);
	}

	// 5. Every track is a point of the scene behind the board: none of the board's features got in.
	// Measured: 2 of 503 off it, and both are wrong matches between scene features the board hid in 2
	// of 5 sampled frames, not the board's: one 275 mrad out, one a two-camera match 138 px along its
	// epipolar line, which the plane sees as 7.8 mrad. Every other member within 15.9 px.
	const rendered::StillScene still = rendered::stillScene(rig, tracks);
	CHECK(still.off * 100 <= tracks.tracks.size());

	// 6. Both results against their profiles. Measured, targetless against registration-targetless/1's
	// Ready: 220 tracks for the fewest-shared camera (100), no weak bridge, 0.72 mrad held out (2),
	// a rotation spread of 0.53 mrad (1) and a translation spread of 0.034% of depth (0.5%), and 15
	// outliers in 1042 observations (5%); a seed angle of 7.6 degrees, 503 tracks, 402 fitted. Board
	// against registration/1: 0.083 mrad held out, a rotation spread of 0.25 mrad, 0.027% (0.46 mm).
	CHECK(targetless.verdict == Verdict::Ready);
	CHECK(board.verdict == Verdict::Ready);
}

TEST_CASE("the rig's feature tracks measured against their truth", "[camera][registration][comparison]")
{
#ifndef NDEBUG
	SKIP("the comparison runs in Release builds: a Debug build takes seconds to draw each HD frame");
#endif
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
