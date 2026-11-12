// One rig registered by both methods from the same footage, through both real backends: the committed
// rig plan apps/rigplan/examples/two-methods.json (four fixed cameras round a textured room, a printed
// board moving through their shared view), rendered by lain::rigplan::writeCapture on every kind of
// device the machine offers (gpu.h) and read back as a user reads a capture: its rig/ footage, its
// truth/models, truth/rig.json, truth/poses.json and truth/board.json. Board registration finds the
// board and places the cameras metrically; targetless registration finds the room, drops the board as
// moving content, and places them up to scale. Both results are validated on the same held-out feature
// tracks (registration::validate) and compared with each other and with the truth. The rig's tracks
// measured against their truth stay on the per-ray footage (test_tracktruth.cpp says why).
//
// Where a pixel's feature came from is asked of the scene the capture was rendered from, on the CPU
// (lain::rigplan::testing's cast): the still scene behind the board, or the whole scene at a frame.
//
// 1280x720 at a focal length of 1000, the operating point registration-targetless/1's angles were set
// for, so the case runs in Release only: a Debug build takes minutes to detect and extract.
//
// It also makes the claims of the targetless end-to-end case it replaced (three cameras over the
// per-ray scene with a disc crossing it, test_targetlesse2e.cpp until 2026-11-11): Ready, the backends
// recorded, the truth bounds, the held-out transfer, and no moving content in the tracks. What that
// case had and this one does not: it ran in Debug, so no Debug build now registers a rig without a
// target through the real backends; and its scene was test_features.cpp's footage case exactly, so its
// tracks read beside that case's.

#include "gpu.h"
#include "registration.h"

#include <lain/camera/board/specification.h>
#include <lain/camera/capture/capturegroup.h>
#include <lain/camera/capture/location.h>
#include <lain/camera/comparison.h>
#include <lain/camera/feature/extraction.h>
#include <lain/camera/projection.h>
#include <lain/camera/registration/board.h>
#include <lain/camera/registration/targetless.h>
#include <lain/camera/registration/validation.h>
#include <lain/camera/serialize/cameramodel.h>
#include <lain/camera/serialize/registeredrig.h>
#include <lain/core/uri.h>
#include <lain/data/value.h>
#include <lain/io/data/load.h>
#include <lain/io/sequence/open.h>
#include <lain/math/angles.h>
#include <lain/rigplan/capture.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/scene.h>
#include <lain/rigplan/serialize/truth.h>
#include <lain/rigplan/testing/capture.h>
#include <lain/rigplan/testing/exampleplan.h>
#include <lain/rigplan/testing/scenecast.h>
#include <lain/rigplan/truth.h>
#include <lain/testing/scratch.h>
#include <lain/testing/skip.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using namespace lain;
using namespace lain::camera;
namespace fs = std::filesystem;

constexpr std::size_t kFrames = 16;

// A document of the capture, read through the io seam.
static data::Value document(const fs::path& file)
{
	std::optional<data::Value> value = io::data::load(core::Uri::fromPath(file));
	INFO(file.string());
	REQUIRE(value.has_value());
	return *value;
}

// two-methods' capture, rendered on `device` into `dir` and opened as M9 opens a rig: each camera's
// footage named by its folder, its model its truth/models document, grouped by position.
struct Capture
{
	rigplan::testing::CaptureScene scene; // what it was rendered from, which the casts ask
	std::vector<capture::CameraIdentity> identities;
	std::vector<registration::RigFootage> footage;
	std::vector<capture::CaptureGroup> groups;
	std::vector<rigplan::CameraPose> poses;	  // truth/poses.json
	registration::RegisteredRig truth;		  // truth/rig.json
	std::vector<rigplan::BoardTruth> board;	  // truth/board.json, frames ascending
	std::optional<board::Specification> spec; // the plan's board
};

static Capture captureOf(acm::Device& device, const fs::path& dir)
{
	fixture::ensureRegistered();
	const rigplan::RigPlan plan = rigplan::testing::examplePlan("two-methods");
	REQUIRE(plan.timeline.frames == kFrames);
	fs::remove_all(dir);
	rigplan::CaptureOptions options;
	options.out = dir / "capture";
	rigplan::testing::renderCapture(plan, device, options);
	const fs::path capture = options.out;

	Capture c;
	c.scene = rigplan::testing::captureScene(plan, options);
	std::vector<media::FrameSequence> sequences;
	for (const rigplan::RealisedCamera& camera : c.scene.realisation.cameras)
	{
		std::optional<media::FrameSequence> footage = io::sequence::open(core::Uri::fromPath(capture / "rig" / camera.name));
		INFO(camera.name);
		REQUIRE(footage.has_value());
		sequences.push_back(*footage);
	}
	c.identities = capture::identitiesByLocation(sequences);
	std::vector<capture::CameraFootage> byPosition;
	for (std::size_t i = 0; i < sequences.size(); ++i)
	{
		const CameraModelRead model = cameraModelFromValue(document(capture / "truth" / "models" / (c.identities[i].value + ".json")));
		REQUIRE(model.model.has_value());
		c.footage.push_back({c.identities[i], *model.model, sequences[i]});
		byPosition.push_back({c.identities[i], sequences[i]});
	}
	const capture::GroupingResult grouping = capture::groupsByPosition(byPosition);
	REQUIRE(grouping.groups.size() == kFrames);
	c.groups = grouping.groups;

	const rigplan::PosesRead poses = rigplan::posesFromValue(document(capture / "truth" / "poses.json"));
	REQUIRE(poses.poses.has_value());
	c.poses = *poses.poses;
	const registration::RegisteredRigRead rig = registration::registeredRigFromValue(document(capture / "truth" / "rig.json"));
	REQUIRE(rig.rig.has_value());
	c.truth = *rig.rig;
	const rigplan::BoardTruthRead board = rigplan::boardTruthFromValue(document(capture / "truth" / "board.json"));
	REQUIRE(board.truth.has_value());
	c.board = *board.truth;
	REQUIRE(c.board.size() == kFrames);

	// The scene the casts ask is the one the capture was rendered from: its build's cameras are where
	// the capture says they were, and each folder is named as the build names its camera.
	REQUIRE(c.poses.size() == c.scene.realisation.cameras.size());
	for (std::size_t i = 0; i < c.poses.size(); ++i)
	{
		const rigplan::RealisedCamera& built = c.scene.realisation.cameras[i];
		CHECK(c.identities[i].value == built.name);
		CHECK(c.poses[i].name == built.name);
		CHECK(math::length(c.poses[i].planFromCamera.translation() - built.planFromCamera.translation()) < 1e-12);
		CHECK(math::rotationBetween(c.poses[i].planFromCamera.rotation(), built.planFromCamera.rotation()) < 1e-12);
	}

	const camera::board::PatternResult pattern = camera::board::Pattern::create(plan.boards.front().pattern);
	REQUIRE(pattern.pattern.has_value());
	const camera::board::SpecificationResult spec = camera::board::Specification::create(*pattern.pattern, plan.boards.front().instance);
	REQUIRE(spec.specification.has_value());
	c.spec = *spec.specification;
	return c;
}

static std::vector<registration::RigCamera> camerasOf(const Capture& c)
{
	std::vector<registration::RigCamera> out;
	for (const registration::RigFootage& f : c.footage)
		out.push_back({f.camera, f.model});
	return out;
}

// The camera of the build a capture's camera was rendered as.
static const rigplan::RealisedCamera& realised(const Capture& c, const capture::CameraIdentity& camera)
{
	for (const rigplan::RealisedCamera& r : c.scene.realisation.cameras)
	{
		if (r.name == camera.value)
			return r;
	}
	FAIL("no camera of the build is named " << camera.value);
}

// Where a realised camera sees a plan-frame point, or nothing.
static std::optional<math::Vec2d> projectInto(const rigplan::RealisedCamera& camera, const math::Vec3d& point)
{
	const math::Vec3d inCamera = camera.planFromCamera.inverse().apply(point);
	const Projection<double> p = project(camera.model, inCamera.x, inCamera.y, inCamera.z);
	if (!p.ok())
		return std::nullopt;
	return math::Vec2d{p.u, p.v};
}

// One camera's corners in view at one frame, from truth/board.json.
static const std::vector<rigplan::CornerTruth>& cornersAt(const Capture& c, std::size_t frame, const capture::CameraIdentity& camera)
{
	for (const rigplan::CameraCorners& corners : c.board.at(frame).cameras)
	{
		if (corners.name == camera.value)
			return corners.inView;
	}
	FAIL("truth/board.json names no camera " << camera.value << " at frame " << frame);
}

struct StillScene
{
	std::size_t off = 0; // tracks that are no point of the still scene
	double worst = 0;	 // pixels, the furthest a member of any other track lands from its point
};

// Whether `tracks` are points of the scene without its moving content: each track's first member
// cast into the still scene the capture was rendered from (staticScene over its build), and the
// point found projected into its other members' cameras. A track off by more than `tolerance` px is
// moving content that got in. Validation against the true rig cannot tell: a synchronised rig sees
// a moving feature at one instant from every camera, which is a consistent point at that instant.
// The still scene can, since behind the board is a wall or the box, a metre or more further away.
static StillScene stillScene(const Capture& c, const feature::TrackSet& tracks, double tolerance = 20.0)
{
	const rigplan::SceneResult still = rigplan::staticScene(c.scene.plan, c.scene.realisation, c.scene.assets);
	REQUIRE(still.problems.empty());
	StillScene out;
	for (const feature::Track& t : tracks.tracks)
	{
		const feature::SceneObservation& first = t.observations.front();
		const std::optional<rigplan::testing::SceneHit> hit =
			rigplan::testing::cast(still, realised(c, tracks.views[first.view].camera), first.pixel);
		double furthest = hit ? 0.0 : std::numeric_limits<double>::infinity();
		for (std::size_t o = 1; o < t.observations.size() && hit; ++o)
		{
			const feature::SceneObservation& other = t.observations[o];
			const std::optional<math::Vec2d> p = projectInto(realised(c, tracks.views[other.view].camera), hit->point);
			furthest = std::max(furthest, p ? math::length(*p - other.pixel) : std::numeric_limits<double>::infinity());
		}
		if (furthest > tolerance)
			++out.off;
		else
			out.worst = std::max(out.worst, furthest);
	}
	return out;
}

static registration::HeldOutEvidence evidence(const std::variant<registration::HeldOutEvidence, camera::Unavailable>& v)
{
	const auto* e = std::get_if<registration::HeldOutEvidence>(&v);
	if (!e)
		FAIL("validation unavailable: " << std::get<camera::Unavailable>(v).reason);
	return *e;
}

// A camera's RMS transfer residual over every member it saw: what a camera's error does to its
// evidence.
static double perCamera(const registration::HeldOutEvidence& e, const capture::CameraIdentity& camera)
{
	for (const registration::HeldOutCamera& c : e.perCamera)
	{
		if (c.camera == camera)
			return c.rmsAngle;
	}
	FAIL("no evidence for " << camera.value);
}

// Of `ids`, those whose track has `members` observations (or more, with `orMore`).
static std::vector<std::string> withMembers(const feature::TrackSet& set, const std::vector<std::string>& ids,
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

TEST_CASE("one rig registered with a board and without one agrees with itself and its truth",
		  "[camera][registration][comparison][gpu]")
{
	lain::testing::skipInDebug("the comparison runs in Release builds: a Debug build takes minutes to detect and extract");
	acm::Device& device = lain::render::testing::device();
	const lain::testing::ThreadPool pool;
	const Capture c = captureOf(device, lain::testing::scratchDir() / "comparison");
	const std::vector<registration::RigCamera> cameras = camerasOf(c);

	// Both methods over the same footage and capture groups, relative to the same camera. Measured, each
	// with its render: 15.5 s on an RTX 4090, 48.1 s on the CPU device (two threads), which renders the
	// 64 frames in about 43 s.
	registration::Request request;
	request.reference = c.identities[1];
	const registration::Report board = registration::board::registerCameras(c.footage, c.groups, *c.spec,
																			camera::board::DetectionRequest{}, request);
	INFO(board.toString());
	REQUIRE(board.status == registration::RegistrationStatus::Succeeded);
	const registration::Report targetless =
		registration::targetless::registerCameras(c.footage, c.groups, feature::ExtractionRequest{}, request);
	INFO(targetless.toString());
	REQUIRE(targetless.status == registration::RegistrationStatus::Succeeded);
	const registration::TargetlessRecord& record = std::get<registration::TargetlessRecord>(targetless.reproducibility.method);
	CHECK(record.extractor.backend == "opencv");
	CHECK(record.matcher.backend == "opencv");
	CHECK(record.geometry.backend == "opencv");
	CHECK(targetless.reproducibility.refiner.backend == "ceres");
	CHECK(board.reproducibility.refiner.backend == "ceres");
	const registration::TargetlessDiagnostics& t = std::get<registration::TargetlessDiagnostics>(targetless.diagnostics.method);
	REQUIRE(t.trackSet.has_value());
	const feature::TrackSet& tracks = *t.trackSet;

	// 1. What the footage must be for the comparison to mean anything.
	// Every camera sees the board in nearly every frame, so board registration has the capture groups
	// its Ready needs. Measured: all 16, in every camera, on both devices.
	const registration::BoardDiagnostics& b = std::get<registration::BoardDiagnostics>(board.diagnostics.method);
	for (const capture::CameraIdentity& id : c.identities)
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
	// sight. Measured, from truth/board.json: every corner in view at least 22.0 px from where it was.
	double slowest = 1e9;
	for (std::size_t v = 0; v < tracks.views.size(); ++v)
	{
		const std::vector<media::FrameRef>& sampled = tracks.views[v].frames;
		for (std::size_t i = 0; i < sampled.size(); ++i)
		{
			for (std::size_t j = i + 1; j < sampled.size(); ++j)
			{
				for (const rigplan::CornerTruth& a : cornersAt(c, sampled[i].ordinal, tracks.views[v].camera))
				{
					for (const rigplan::CornerTruth& z : cornersAt(c, sampled[j].ordinal, tracks.views[v].camera))
					{
						if (a.corner == z.corner)
							slowest = std::min(slowest, math::length(a.pixel - z.pixel));
					}
				}
			}
		}
	}
	CHECK(slowest > 10.0);
	// What the board hides in most of the sampled frames, where no static feature can come from, cast
	// into the scene at each sampled frame. Measured: at most 4.6% of an image, over 5 sampled frames.
	std::vector<rigplan::SceneResult> atFrame;
	for (std::size_t frame = 0; frame < kFrames; ++frame)
		atFrame.push_back(rigplan::renderScene(c.scene.plan, c.scene.realisation, c.scene.assets, frame));
	double hidden = 0;
	for (std::size_t v = 0; v < tracks.views.size(); ++v)
	{
		const rigplan::RealisedCamera& camera = realised(c, tracks.views[v].camera);
		std::size_t points = 0, covered = 0;
		for (int y = 8; y < 720; y += 16)
		{
			for (int x = 8; x < 1280; x += 16)
			{
				std::size_t on = 0;
				for (const media::FrameRef& frame : tracks.views[v].frames)
				{
					const std::optional<rigplan::testing::SceneHit> hit =
						rigplan::testing::cast(atFrame.at(frame.ordinal), camera, math::Vec2d{double(x), double(y)});
					on += hit && hit->owner.object == rigplan::SceneObject::Board ? 1 : 0;
				}
				covered += on * 2 > tracks.views[v].frames.size() ? 1 : 0;
				++points;
			}
		}
		hidden = std::max(hidden, double(covered) / double(points));
	}
	CHECK(hidden < 0.1);

	// 2. Each method against the truth, and the two against each other, in the truth's metres. The
	// truth is relative to the rig plan's first camera and both methods chose another, so the
	// comparison derives the truth again for theirs. Measured, on a hardware GPU and on the CPU device:
	// - the board result 0.15 and 0.20 mrad and 0.34 and 0.41 mm from the truth once scaled, by 1.00014
	//   and 1.00018; unscaled, its centres 0.26 and 0.33 mm out;
	// - the targetless result 1.02 and 0.25 mrad and 2.72 and 0.87 mm, scaled by 4.219 and 4.223;
	// - the two 0.91 and 0.34 mrad and 2.72 and 1.03 mm apart.
	// The targetless result moves with the device as much as with the seed (WORK.md, *Fixed-rig footage
	// from committed rig plans*), so its bounds keep a hardware GPU's margin. The cameras stand 3.2 m
	// from the room's far side and 0.6 m apart, the board 1.6 m away.
	const std::optional<registration::RegisteredRig> byBoard = board.rig();
	const std::optional<registration::RegisteredRig> byScene = targetless.rig();
	REQUIRE(byBoard.has_value());
	REQUIRE(byScene.has_value());
	CHECK(byBoard->reference == c.identities[1]);
	CHECK(byScene->reference == c.identities[1]);
	CHECK(c.truth.reference == c.identities[0]);
	const RigComparison boardTruth = compareRigs(c.truth, *byBoard);
	REQUIRE(boardTruth.compared());
	// Unscaled, against the truth relative to the board's own reference, derived from truth/poses.json.
	const registration::RegisteredRig truthAtReference = rigplan::rigRelativeTo(c.poses, byBoard->reference.value);
	double unscaled = 0;
	for (std::size_t i = 0; i < truthAtReference.cameras.size(); ++i)
	{
		unscaled = std::max(unscaled, math::length(byBoard->cameras[i].referenceFromCamera.translation() -
												   truthAtReference.cameras[i].referenceFromCamera.translation()));
	}
	CHECK(std::abs(boardTruth.scale - 1.0) < 0.001);
	CHECK(boardTruth.worstRotation()->rotation < 0.001);
	CHECK(unscaled < 0.002);
	const RigComparison sceneTruth = compareRigs(c.truth, *byScene);
	REQUIRE(sceneTruth.compared());
	CHECK(sceneTruth.worstRotation()->rotation < 0.0015);
	CHECK(sceneTruth.worstCentre()->centre < 0.004);
	const RigComparison between = compareRigs(*byBoard, *byScene);
	REQUIRE(between.compared());
	CHECK(between.worstRotation()->rotation < 0.0015);
	CHECK(between.worstCentre()->centre < 0.004);
	for (const RigComparison* comparison : {&boardTruth, &sceneTruth, &between})
	{
		CHECK(comparison->onlyExpected.empty());
		CHECK(comparison->onlyFound.empty());
	}

	// 3. Both validated on the tracks the targetless registration held out: the same predictions of
	// the same members, so the two numbers compare one method with the other. Held-out transfer sees
	// what the tracks can see: a camera turned (below), not the rig's weak mode, which is what the
	// bootstrap's rotation spread is for. Measured: 55 and 54 tracks, 150 and 147 members; 0.69 and
	// 0.66 mrad for the board's result, 0.72 and 0.65 for the targetless one.
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
	// where a two-member track cannot see it, and a tilt takes them out. Either way the rig's RMS
	// crosses Ready's 2 mrad, but only because it has four cameras. That is why fitness judges each
	// camera's held-out residual too since 2026-10-20 (FitnessThresholds), so one such camera among
	// many is seen. Measured, cam02's transfer and the rig's RMS on a hardware GPU, then the CPU device:
	// - pan: cam02 4.13 and 3.98 mrad, the rig 4.71 and 4.81; in two-member tracks 0.67 -> 0.72 and
	//   1.22 -> 1.46, in longer ones 0.45 -> 4.97 and 0.19 -> 5.00;
	// - tilt: cam02 5.43 and 5.79 mrad, the rig 3.76 and 4.30; in two-member tracks 0.67 -> 6.6 and
	//   1.22 -> 7.0, in longer ones 0.45 -> 4.77 and 0.19 -> 4.79.
	// So a pan is judged by how little it moves two-member tracks, not by where they end: about 25 of
	// them per camera, and on the CPU device's footage they start above 1 mrad.
	const std::vector<std::string> pairs = withMembers(tracks, held, 2, false);
	const std::vector<std::string> longer = withMembers(tracks, held, 3, true);
	const double untouchedTwo = perCamera(evidence(registration::validate(cameras, board, tracks, pairs)), c.identities[2]);
	for (const bool tilt : {false, true})
	{
		CAPTURE(tilt);
		registration::Report turned = board;
		for (registration::RegisteredCamera& camera : turned.cameras)
		{
			if (camera.camera == c.identities[2])
				camera.referenceFromCamera = camera.referenceFromCamera *
											 math::RigidTransformd{math::angleAxis(0.005, tilt ? math::Vec3d{1, 0, 0} : math::Vec3d{0, 1, 0}),
																   math::Vec3d{0.0}};
		}
		const registration::HeldOutEvidence all = evidence(registration::validate(cameras, turned, tracks, held));
		const registration::HeldOutEvidence two = evidence(registration::validate(cameras, turned, tracks, pairs));
		const registration::HeldOutEvidence more = evidence(registration::validate(cameras, turned, tracks, longer));
		CHECK(perCamera(all, c.identities[2]) > (tilt ? 4.5e-3 : 3e-3));
		CHECK(all.rmsAngle > 0.002);
		CHECK(perCamera(more, c.identities[2]) > 4e-3);
		if (tilt)
			CHECK(perCamera(two, c.identities[2]) > 5e-3);
		else
			CHECK(perCamera(two, c.identities[2]) - untouchedTwo < 0.5e-3);
	}

	// 5. Every track is a point of the room behind the board: none of the board's features got in.
	// Measured: 2 of 274 and 3 of 269 off it, and none is the board's, which covers their pixels in at
	// most 2 of 5 sampled frames. Each is a wrong match between features of the floor or the far wall
	// that are not one point: its members' rays meet the room 73 mm to 1.4 m apart. Validation against
	// the truth sees one of the five (12 mrad out); the other four are two-member tracks slid along
	// their epipolar line, which the plane they are scored against cannot see. Every other member
	// within 17.2 px. With extraction's static rule disabled, 825 of 1191 and 733 of 1097 tracks are
	// off it.
	const StillScene still = stillScene(c, tracks);
	CHECK(still.off * 50 <= tracks.tracks.size());

	// 6. Both results against their profiles. Ready on both devices here. On macOS arm64's features
	// (OpenCV's SIFT finds others there; CI, 2026-11-12) the targetless result was
	// Exploratory on cam3's resampled rotation variation alone, 1.03 mrad against Ready's 1, with every
	// check above held. Either verdict is the registration's; that is the only shortfall allowed.
	if (targetless.verdict != Verdict::Ready)
	{
		CHECK(targetless.verdict == Verdict::Exploratory);
		REQUIRE(targetless.fitnessNotes.size() == 1);
		CHECK_THAT(targetless.fitnessNotes.front(), ContainsSubstring("rotation variation"));
	}
	CHECK(board.verdict == Verdict::Ready);
}
